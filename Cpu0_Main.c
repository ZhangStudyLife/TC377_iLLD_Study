#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "Ifx_Cfg_Ssw.h"
#include "Port/Std/IfxPort.h"
#include "Bsp.h"
#include "App/Hardware/Led.h"
#include "App/Hardware/Key.h"
#include "App/Hardware/ips200.h"


#include "IfxGtm_Tom_Pwm.h" // GTM TOM PWM 驱动
#include "IfxStm_Timer.h"   // STM 周期中断驱动

/* 时钟学习入口（频率查询结果统一为 Hz；1 MHz = 1000000 Hz）
 * SCU = System Control Unit，系统控制单元；CCU = Clock Control Unit，时钟控制单元。
 * SCU 的 CCU 管理系统时钟，与捕获比较定时器 CCU6 不是同一个模块。
 * XTAL 是外部晶振；PLL = Phase-Locked Loop，锁相环，用参考时钟生成更高频率。
 * 本工程 Configurations/Ifx_Cfg.h：XTAL 20 MHz，系统 PLL 300 MHz，
 * 外设 PLL 两路输出分别为 320 MHz、200 MHz；启动代码在 core0_main 前初始化时钟。
 * 当前默认分频目标：CPU0/1/2 与 SRI 300 MHz，SPB 与 STM 100 MHz，GTM 200 MHz。
 * 这些是配置目标，最终应查询运行时寄存器；分频字段的编码不一定等于数学分母。
 * 查询函数根据寄存器和工程填写的晶振频率计算结果，不是物理频率测量仪。
 *
 * 本例的 PWM 时钟路径：
 * 系统时钟分配 -> GTM 系统时钟 -> Cluster 0 -> CMU -> GCLK
 *                                               -> CLK0 等可配置时钟
 * GCLK 或选中的 CLK0~CLK7 -> FXCLK 固定分频 -> TOM 通道 -> PWM 引脚。
 * 外设的总线接口时钟、内部功能时钟、最终输出频率是不同概念。
 * CPU 写 GTM 寄存器后，TOM 硬件使用自身时钟计数，无需 CPU 逐个生成 PWM 边沿。
 */
#include "IfxScuCcu.h"  // Scu的意思是 System Control Unit，CCU是Clock Control Unit的意思，主要是用来配置时钟的
#include "IfxGtm_Cmu.h" // Gtm的意思是 Generic Timer Module，CMU是Clock Management Unit的意思，主要是用来配置GTM模块的时钟的

// float32 为 32 位浮点数；volatile 要求编译器保留对变量的实际访问，便于观察。
// volatile 不会自动刷新调试窗口，也不保证多核同步；全局变量可加入 Expressions。
volatile float32 cpu0Hz, cpu1Hz, cpu2Hz; // 各 CPU 核时钟；主频不等于每秒执行指令数
volatile float32 ips200StringUs; // 100 轮完整 IPS200 测试的平均耗时，单位 us
// SRI = Shared Resource Interconnect，共享资源互连，连接 CPU、存储器等高带宽资源。
// SPB = System Peripheral Bus，系统外设总线，用于外设寄存器访问等。
// STM = System Timer，系统定时器；GTM = Generic Timer Module，通用定时器模块。
volatile float32 sriHz, spbHz, stmHz, gtmHz;
// CMU = Clock Management Unit，GTM 内部的时钟管理单元。
// GCLK = Global Clock，CMU 全局时钟；CLK0 为可配置时钟，FXCLK 为固定分频时钟。
volatile float32 cmuHz, gclkHz, clk0Hz, fxclk0Hz;

IfxGtm_Tom_Pwm_Driver g_tomPwmDriver;

void Gtm_Pwm_Init(void)
{
    // 1. 使能 GTM 模块（GTM 默认是关闭的，要先打开）
    IfxGtm_enable(&MODULE_GTM);

    // 2. 配置时钟：
    //    本版 iLLD 返回 CMU 输入频率，即 Cluster 0 时钟；不一定等于 GTM 系统时钟
    float32 freq = IfxGtm_Cmu_getModuleFrequency(&MODULE_GTM);
    // 将 CMU 的 GCLK 设为输入频率；此处不再降频，不表示所有 GTM 子模块都直接用 GCLK
    IfxGtm_Cmu_setGclkFrequency(&MODULE_GTM, freq);
    // 将可配置 CLK0 设为 freq；本例 TOM 实际选择 FXCLK0。
    // FXCLK 来源由 CMU.FXCLK.CTRL.FXCLK_SEL 决定：0 选 GCLK，1~8 选 CLK0~CLK7。
    // 仅配置/使能 CLK0 不会自动把 FXCLK 来源切换为 CLK0。
    // FXCLK0/1/2/3/4 对所选来源分别分频 1/16/256/4096/65536。
    IfxGtm_Cmu_setClkFrequency(&MODULE_GTM, IfxGtm_Cmu_Clk_0, freq);
    // 使能 CLK0 和 FXCLK 时钟（TOM 通道实际要用）
    IfxGtm_Cmu_enableClocks(&MODULE_GTM,
                            IFXGTM_CMU_CLKEN_FXCLK | IFXGTM_CMU_CLKEN_CLK0);

    // 3. 配置 TOM PWM：
    IfxGtm_Tom_Pwm_Config config;                    // 配置结构体
    IfxGtm_Tom_Pwm_initConfig(&config, &MODULE_GTM); // 填默认值
    config.tom = IfxGtm_Tom_0;                       // 用 TOM0
    config.tomChannel = IfxGtm_Tom_Ch_5;             // TOM0 通道 5，输出到 P21.4
    config.period = 5000;                            // 周期计数 = 5000 tick，不是 5000 us
    // tick 是通道时钟的一次计数：每 tick 时间 = 1 / fFXCLK0。
    // 当前边沿对齐 PWM：周期 = 5000 / fFXCLK0，频率 = fFXCLK0 / 5000。
    // 若实际 FXCLK0 = 200 MHz，则 PWM = 40 kHz，周期 25 us。
    config.dutyCycle = 2500;                                       // 初始 50%：5000 * 50 / 100 = 2500 tick
    config.clock = IfxGtm_Tom_Ch_ClkSrc_cmuFxclk0;                 // TOM 时钟源 = FXCLK0
    config.pin.outputPin = &IfxGtm_TOM0_5_TOUT55_P21_4_OUT;        // 输出到 P21.4，LED0 是 P20.9
    config.pin.outputMode = IfxPort_OutputMode_pushPull;           // 推挽
    config.pin.padDriver = IfxPort_PadDriver_cmosAutomotiveSpeed1; // P21.4 为 SLOW pad，选较快边沿

    // 4. 初始化并将 P21.4 复用为 TOM 输出
    IfxGtm_Tom_Pwm_init(&g_tomPwmDriver, &config);

    // 5. 启动 PWM
    IfxGtm_Tom_Pwm_start(&g_tomPwmDriver, TRUE);
}

IFX_ALIGN(4)
IfxCpu_syncEvent g_cpuSyncEvent = 0;
/* STM0 每 10 ms 请求 CPU0 执行一次中断，TOM0 则独立持续输出 PWM。
 * 中断频率 = 1 / 0.01 s = 100 Hz；不是 PWM 的输出频率。
 * STM 为 100 MHz 时，10 ms 对应 1000000 个原始 STM tick。
 * 驱动根据实际 STM 时钟和比较位范围计算比较值，无需手写固定 tick 数。
 */
#define ISR_PRIORITY_STM_10MS 2 // 非零中断优先级；必须与 IFX_INTERRUPT 的优先级一致

IfxStm_Timer g_stmTimer;
volatile float32 duty = 50.0f; // 占空比百分比；可在 Expressions 中观察，仅在 ISR 中修改
static sint32 dir = 1;         // +1 增大占空比，-1 减小占空比

void Stm_Timer_Init(void)
{
    IfxStm_Timer_Config config;
    IfxStm_Timer_initConfig(&config, &MODULE_STM0); // 使用 STM0 并填充默认配置
    config.base.frequency = 100.0f;                 // 100 Hz，即每 10 ms 中断一次
    config.base.isrPriority = ISR_PRIORITY_STM_10MS;
    config.base.isrProvider = IfxSrc_Tos_cpu0; // SRC 将中断请求交给 CPU0
    config.base.minResolution = 0.00001f;      // 要求计数分辨率不劣于 10 us，不是中断周期
    config.comparator = IfxStm_Comparator_0;   // 使用 STM0 的比较器 0
    IfxStm_Timer_init(&g_stmTimer, &config);   // 配置比较器、请求路由及中断使能
    IfxStm_Timer_run(&g_stmTimer);             // 安排首次比较匹配，开始周期定时
}

// 第二个参数 0 是 CPU0 的中断向量表编号；第三个参数对应 SRC 中配置的优先级。
IFX_INTERRUPT(Stm_10ms_Isr, 0, ISR_PRIORITY_STM_10MS)
{
    // 先清比较匹配标志，并将“上一次比较值 + 周期”设为下一次比较值。
    // 这样定时基准沿原计划推进，不会把本次 ISR 的执行时间逐次加到周期上。
    IfxStm_Timer_acknowledgeTimerIrq(&g_stmTimer);

    // 每 10 ms 改变 1 个百分点，在 50%~100% 之间往返，单程约 500 ms。
    duty += dir;
    if (duty >= 100.0f)
    {
        duty = 100.0f;
        dir = -1;
    }
    else if (duty <= 50.0f)
    {
        duty = 50.0f;
        dir = 1;
    }

    // 本仓库 setDuty 的单位是百分比；内部换算为 period * duty / 100 个 tick。
    // 初始化 config.dutyCycle 的单位则是 tick，注意区别。
    // 当前默认为直接更新 CM1；CM0 为周期比较值，CN0 为通道计数器。
    IfxGtm_Tom_Pwm_setDuty(&g_tomPwmDriver, duty);
    // ISR 中不延时、不打印，也不主动开启中断嵌套，保持处理过程简短。
}

void delay_ms(float time)
{
    IfxStm_waitTicks(
        &MODULE_STM0,
        IfxStm_getTicksFromMilliseconds(&MODULE_STM0, time));
}

void core0_main(void)
{
    IfxCpu_enableInterrupts();

    /* !!WATCHDOG0 AND SAFETY WATCHDOG ARE DISABLED HERE!!
     * Enable the watchdogs and service them periodically if it is required
     */
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    /* Wait for CPU sync event */
    IfxCpu_emitEvent(&g_cpuSyncEvent);
    IfxCpu_waitEvent(&g_cpuSyncEvent, 1);
    // Gtm_Pwm_Init();
    // Stm_Timer_Init();
    // 先初始化 PWM，再启动 STM，保证首次中断访问的是已初始化的驱动。
    Ips200_Init();
    Ips200_Clear();
    // 测试图像提前生成，图像生成耗时不计入屏幕接口测试。
    static uint16 testImage[64 * 64];
    for (uint16 y = 0; y < 64; ++y)
        for (uint16 x = 0; x < 64; ++x)
            testImage[y * 64 + x] = ((x >> 1) << 11) | (y << 5) | ((x + y) >> 2);
    stmHz = IfxStm_getFrequency(&MODULE_STM0);
    uint64 ips200Ticks = 0;
    // 每轮单独计时，再累加；整批测试可能跨越多次 STM 低 32 位回绕。
    for (int i = 0; i < 5; i++)
    {
        uint32 ips200Start = IfxStm_getLower(&MODULE_STM0);
        Ips200_SetColor(IPS200_WHITE, IPS200_BLACK);
        Ips200_Clear();
        // 全屏基本颜色测试，最后回到黑色以显示后续图形。
        Ips200_Fill(IPS200_RED);
        Ips200_Fill(IPS200_GREEN);
        Ips200_Fill(IPS200_BLUE);
        Ips200_Fill(IPS200_YELLOW);
        Ips200_Fill(IPS200_CYAN);
        Ips200_Fill(IPS200_MAGENTA);
        Ips200_Fill(IPS200_WHITE);
        Ips200_Fill(IPS200_BLACK);

        Ips200_DrawPoint(0, 0, IPS200_RED);
        Ips200_DrawPoint(239, 0, IPS200_GREEN);
        Ips200_DrawPoint(0, 319, IPS200_BLUE);
        Ips200_DrawPoint(239, 319, IPS200_WHITE);
        Ips200_DrawLine(8, 96, 231, 96, IPS200_YELLOW);   // 水平
        Ips200_DrawLine(8, 100, 8, 190, IPS200_CYAN);     // 垂直
        Ips200_DrawLine(16, 112, 104, 184, IPS200_RED);   // 正斜率
        Ips200_DrawLine(104, 112, 16, 184, IPS200_BLUE);  // 反向、负斜率
        Ips200_DrawLine(48, 112, 64, 184, IPS200_MAGENTA); // 陡斜率
        Ips200_DrawLine(104, 148, 16, 160, IPS200_CYAN);  // 缓斜率
        Ips200_DrawRect(120, 112, 104, 80, IPS200_WHITE);
        Ips200_FillRect(136, 128, 72, 48, IPS200_GREEN);

        Ips200_ShowChar(8, 8, 'A');
        Ips200_ShowString(24, 8, "IPS200 RGB565 TEST");
        Ips200_ShowInt(8, 32, -2147483647 - 1);
        Ips200_ShowUint(8, 48, 4294967295U);
        Ips200_ShowFloat(8, 64, -123.456789, 6);
        Ips200_ShowRgb565Image(16, 216, testImage, 64, 64);
        ips200Ticks += (uint32)(IfxStm_getLower(&MODULE_STM0) - ips200Start);
    }
    // 包含发生的中断和少量读计数开销；不要在计时区间单步或设断点。
    ips200StringUs = (float32)((float64)ips200Ticks * 1000000.0 / stmHz / 100.0);
    while (1)
    {
        Ips200_ShowString(0, 304, "AVG ms: ");
        Ips200_ShowFloat(64, 304, ips200StringUs / 5.0, 2);

    }
}
