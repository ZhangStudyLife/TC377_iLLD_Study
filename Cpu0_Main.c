/**********************************************************************************************************************
 * 文件名: Cpu0_Main.c
 *
 * 学习主题: TC377 (TriCore) iLLD 入门 —— GPIO 输出翻转实验
 *
 * 行为: CPU0 将 P21.4 配置为推挽输出, 并以 1 秒间隔翻转该引脚电平。
 *       可作为 LED 闪烁 / 示波器测波形的最小工程。
 *
 * 实验目的:
 *   1. 理解 iLLD 启动流程中 CPU 同步、看门狗关闭的具体做法;
 *   2. 深入 IfxPort 引脚模式 (IOCR)、输出锁存/翻转 (OMR) 以及 Pad
 *      驱动强度 (PDR) 三层配置;
 *   3. 理解 STM (System Timer) 的阻塞延时原理 (读 TIM0 做无符号差运算)。
 *
 * 硬件连接: P21.4 输出 1Hz 方波 (建议示波器/逻辑分析仪观察; 若接板载
 *           LED 请先核对原理图对应引脚, 本代码只配置 P21.4)。
 *
 * 与 STM32 的差异: 引脚 GPIO 模式由 IOCR (每脚 8 位, 即 4 位选择输入,
 *           4 位选择输出), 而非 STM32 的 MODE/OTYPE/PUPD/AF 多组寄存器;
 *           引脚输出驱动能力由 PDR 单独配置。见下方逐行注释。
 *
 * 注意: 本文件默认关闭 CPU 看门狗与安全看门狗, 仅为学习方便; 实际工程
 *       需按需求重新开启并周期喂狗。
 *********************************************************************************************************************/

#include "Ifx_Types.h"      /* iLLD 基础类型: boolean/uint/sint 等 */
#include "IfxCpu.h"         /* CPU 相关: 中断开关、启动同步事件、核号查询等 */
#include "IfxScuWdt.h"      /* 系统控制单元看门狗: 密码读取、Endinit 与喂狗 */
#include "Ifx_Cfg_Ssw.h"    /* iLLD 启动软件配置 */
#include "Port/Std/IfxPort.h" /* 端口外设驱动: 引脚模式/电平/Pad 驱动配置函数原型 */
#include "Bsp.h"            /* 板级支持包: 默认延时等 (本示例未直接调用) */

/* g_cpuSyncEvent: 三核启动同步用的一个 32 位共享变量。
 * 每个核进入 coreX_main() 时会通过 IfxCpu_emitEvent() 置位自己的核位,
 * 最终所有核都置位后, 各核退出 IfxCpu_waitEvent() 的循环, 完成启动同步。
 * 4 字节对齐 (IFX_ALIGN(4)) 是避免跨总线拆分访问, 并满足该变量的访问原子性。
 * 该变量要求所有核都能访问到 (编译时放在默认全局内存/本地 DSPR 可寻址区域)。 */
IFX_ALIGN(4) IfxCpu_syncEvent g_cpuSyncEvent = 0;

/* CPU0 的入口 main: 这是工程模板中三个 cpu main 之一, 由 iLLD 启动代码调用。
 * 三核模板的启动顺序为 core0 -> core1 -> core2, 各自独立的入口。 */
void core0_main(void)
{
    /* 使能 CPU0 的中断 (CPS/IE 位)。
     * IfxCpu_enableInterrupts() 只是执行 __enable() (即 TriCore 的 ENABLE 指令),
     * 它会置位 CPU 状态寄存器中的全局中断使能位, 后续 SRC 某节点分配给 CPU0
     * 且该外设使能后即可进入中断。若这里不调用, 即使外设配好中断也不会响应。 */
    IfxCpu_enableInterrupts();

    /* !!WATCHDOG0 AND SAFETY WATCHDOG ARE DISABLED HERE!!
     * Enable the watchdogs and service them periodically if it is required
     */
    /* 关闭 CPU0 的看门狗。
     * 参数是当前 CPU0 看门狗的密码: 硬件在读取 CON0.PW 时会把低 6 位取反,
     * 因此 IfxScuWdt_getCpuWatchdogPassword() 内部会 pw ^= 0x3F 还原真实密码。
     * 关闭动作 = 先清除 Endinit 保护(允许写保护寄存器), 将 CON1.DR 置 1
     * (Disable/Reload -> 停用看门狗), 再重新置位 Endinit。
     * 看门狗用于防止代码跑飞后的周期性复位; 学习阶段关闭便于死循环调试。 */
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());

    /* 关闭安全看门狗(系统看门狗)。
     * 安全看门狗独立于各 CPU 看门狗, 它监控整个系统, 需要使用独立的安全密码
     * IfxScuWdt_getSafetyWatchdogPassword()。关闭同样需要清除安全 Endinit。
     * 是 TC3xx "功能安全" 概念的一部分, 普通 STM32 没有这个层次。 */
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    /* 等待 CPU 同步事件: 通知其它核我已就绪, 并等待所有核都达到这里。
     * IfxCpu_emitEvent() 用原子读写指令把 (1 << coreId) 置入 g_cpuSyncEvent;
     * IfxCpu_waitEvent() 轮询该变量直到 IFXCPU_CFG_ALLCORE_DONE(0x7, 即
     * 三核都到达)或等待超时(第 2 参为毫秒超时, 这里 1ms)。
     * 为的是保证三核在进入用户业务代码之前彼此同步。 */

    // g_cpuSyncEvent就是一个 unsigned int 的变量 , 然后只有3个核心都调用了IfxCpu_emitEvent,后面在IfxCpu_waitEvent进行死等 , 然后参数是超时
    IfxCpu_emitEvent(&g_cpuSyncEvent);
    IfxCpu_waitEvent(&g_cpuSyncEvent, 1);


    /* ==================== GPIO 实验 ==================== */
    /* 端口选择: 指向 P21 端口外设寄存器基地址。
     * MODULE_P21 是 iLLD 给 P21 端口寄存器块起的宏名 (SFR 声明映射到
     * 0xF003_xxxx 左右的外设总线空间)。 */
    #define TEST_PORT_1 &MODULE_P21
    /* 引脚号: P21 端口上的第 4 脚 (通常写成 P21.4, 即 21 端口的 bit4)。 */
    #define TEST_PIN_1 4

    /* 先把输出锁存器(Pn_OUT)第 4 位预置为高。
     * IfxPort_setPinHigh() -> setPinState(port, pin, high) -> 写 OMR 寄存器:
     *   OMR.PS(pin)  = 1, OMR.PC(pin) = 0  => 该引脚输出被置为 1。
     * OMR 是 "端口输出修改寄存器": 对 PS/PC 位写 1 会对应该置位/清零, 写 0 不改变,
     * 因此它是免读-改-写的原子操作, 比直接写 Pn_OUT 更安全(不会影响其他引脚)。
     * 先置为高再切模式, 可避免切换为输出的瞬间引脚先闪一下低电平。 */
    IfxPort_setPinHigh(TEST_PORT_1, TEST_PIN_1);

    /* 配置 P21.4 为推挽输出, 选择通用输出索引。
     * IfxPort_setPinModeOutput(port, pin, mode, index)
     *   mode = IfxPort_OutputMode_pushPull (推挽, 可强驱动高低电平)
     *   index= IfxPort_OutputIdx_general (通用 I/O 输出, 非某个外设功能口)
     * 实际执行链: setPinModeOutput -> setPinMode(port, pin, (mode|index)):
     *   (index|mode) 拼成 IfxPort_Mode 枚举值, 写入该引脚对应的 IOCR 寄存器
     *   IOCR0/1/2/3 的 8 位字段 (IOCR = 输入/输出控制寄存器):
     *     低 4 位选择输入模式+输出模式, 高 4 位选择输出索引(输出复用)。
     *   讲义: TC3xx 的每个 IO 引脚引脚模式只有一个 8 位 IOCR 字段, 同时编码
     *   了 "输入方向/输出方向/以及连到哪个外设输出", 而不是 STM32 的多组配置。 */
    IfxPort_setPinModeOutput(TEST_PORT_1, TEST_PIN_1, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);

    /* ===== 配置 P21.4 的 Pad 驱动 (IfxPort_setPinPadDriver) =====
     * 背景: 每个引脚的 Pad 特性由该端口 PDR 寄存器中的 4 位 (PDR0/PDR1 中
     *       PLx[3:2] | PDx[1:0]) 控制, 两者相互独立:
     *       - PDx[1:0] (低 2 位): 选择【输出驱动强度 + 压摆率(边沿陡峭度)】,
     *         即 Speed Grade (速度等级);
     *       - PLx[3:2] (高 2 位): 选择【输入电平阈值 VIH/VIL】, 同时改变该
     *         引脚内部上/下拉电阻的阻值。
     *
     * IfxPort_PadDriver 枚举就是把 (PL<<2)|PD 打包成 0~15 的一个数:
     *   cmosAutomotiveSpeed1..4 = 0,1,2,3   (PL=00 -> Automotive 电平 "AL")
     *   ttlSpeed1..4            = 8,9,10,11  (PL=10 -> TTL 电平, 5V 电源)
     *   ttl3v3Speed1..4         = 12..15     (PL=11 -> TTL 电平, 3.3V 电源)
     * 写入方式: 取 PDR[pin/8] 寄存器, 用 __ldmst 原子写第 pin%8 对应的 4 位;
     * PDR 受 Endinit 保护, 函数内部自动 清 Endinit -> 写 -> 恢复。
     *
     * 【PL 高 2 位 -> 输入电平阈值】(手册 Table 632 "Pad Level Selection"):
     *   PL = 0b0X (cmosAutomotive 系列) : Automotive 电平 "AL", 阈值较宽,
     *      适合汽车线束/传感器等可能被拉到不同幅度的信号;
     *   PL = 0b10 (ttlSpeed1..4)       : TTL 电平, 面向 5V 电源, 与 5V
     *      逻辑器件混接时用;
     *   PL = 0b11 (ttl3v3Speed1..4)    : TTL 电平, 面向 3.3V 电源, 与
     *      大多数 3.3V 数字芯片接口时用 (当前实验即此)。
     *   PLx.1 同时还改变内部上/下拉电阻阻值 (见数据手册 Pad 电气参数)。
     *
     * 【PD 低 2 位 -> 输出速度等级 Speed Grade】(手册 Table 629/630/631):
     *   PD = 0b00 -> Speed 1: 驱动最弱, 边沿最缓 (拉电流/灌电流最小, EMI 最低)
     *   PD = 0b01 -> Speed 2
     *   PD = 0b10 -> Speed 3
     *   PD = 0b11 -> Speed 4: 驱动最强, 边沿最陡 (适合高速信号)
     *   注意: 实际可用的档位数取决于引脚所属 Pad 类别 (Strong/Fast/Slow):
     *   Strong Pad 支持 1~4 档, Fast Pad 只到 3 档, Slow Pad 只到 2 档;
     *   P21.4 具体属于哪类 Pad, 需查 TC377 数据手册的引脚分类表。
     *
     * 【怎么选】
     *   - 只翻转电平的 LED / 按键 / 指示灯: Speed 1 或 2 足够, 少 EMI;
     *   - 高速串行 (QSPI/ASC 高波特率) 或需要陡沿的: Speed 4;
     *   - 与 3.3V 芯片通信 -> ttl3v3 系列; 与 5V 逻辑混接 -> ttl 系列;
     *   - 汽车线束 / 传感器信号 -> cmosAutomotive 系列。
     * 当前实验是 1Hz 方波, 用 ttl3v3 + Speed4 并非必需 (仅演示最陡沿),
     * 实际 LED 应用选 ttl3v3Speed1/2 更合理。 */
    IfxPort_setPinPadDriver(TEST_PORT_1, TEST_PIN_1, IfxPort_PadDriver_cmosAutomotiveSpeed1);


    /* 主循环: 每隔 1 秒翻转一次 P21.4 电平, 形成方波。 */
    while(1)
    {
        /* 翻转 P21.4: setPinToggled -> 写 OMR: PS=1, PC=1 => 该脚输出取反。
         * OMR 原子操作, 同一端口其他引脚不受影响。 */
        IfxPort_togglePin(TEST_PORT_1, TEST_PIN_1);

        /* 阻塞延时 1000ms, 基于 STM0 系统定时器计数。
         * 第一参: 指向 STM0 定时器寄存器块 (TC377 每个 CPU 有独立 STM,
         *         这里显式用 STM0, 即 CPU0 的 STM)。
         * 第二参: 要等待的 tick 数 = 1000ms 对应的 tick 数。
         * 换算: IfxStm_getTicksFromMilliseconds(stm, ms) =
         *       (fSTIM/1000) * ms, 其中 fSTIM = STM 输入频率(Hz)。
         *       TC377 上 STM 频率接 SCU 的 STMDIV 分频后时钟,
         *       默认约 100MHz 量级, 因此 1s 大约 100_000_000 tick。
         * IfxStm_waitTicks() 阻塞等待: 记录开始时间 TIM0, 然后
         *       while ((TIM0 - begin) < ticks); 用无符号 32 位减法,
         *       所以即使 TIM0 回绕(溢出), 差值计算依然正确。
         *       这是"绝对时间戳+无符号差"方式的经典写法。
         */
        IfxStm_waitTicks(
            &MODULE_STM0,
            IfxStm_getTicksFromMilliseconds(&MODULE_STM0, 1000)
        );
    }
}
