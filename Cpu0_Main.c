#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxScuCcu.h"
#include "IfxGtm_Tom_Pwm.h"
#include "IfxGtm_Tim_In.h"
#include "IfxStm_Timer.h"
#include "IfxQspi.h"
#include "App/Hardware/ips200.h"
#include "App/Hardware/overclock_test.h"
#include "App/Hardware/platform_test.h"

IFX_ALIGN(4) IfxCpu_syncEvent g_cpuSyncEvent = 0;
volatile uint32 stressLoops[3], stressErrors[3];
volatile float32 cpu0Hz, cpu1Hz, cpu2Hz, sriHz, spbHz, stmHz, gtmHz;
volatile float32 pwmMeasuredHz, pwmMeasuredDuty;
volatile uint32 uptimeSeconds, stmInterrupts, stmLate, stmMaxLateTicks;
volatile uint32 pwmSamples[9], pwmErrors[9], pwmIncoherent, pwmSkipped;
volatile uint32 pwmStage, pwmPeriodTicks, pwmDutyTicks, screenFrames, qspiErrorFlags;
static IfxGtm_Tom_Pwm_Driver pwm;
static IfxGtm_Tim_In capture;
static IfxStm_Timer timer;
static uint32 pwmChangedAt;
static boolean screenPending;

/* 1 kHz STM 中断，记录距离预定比较时间的延迟。
 * GTM 与 STM 都来自系统 PLL，只能做内部一致性验证。
 */
IFX_INTERRUPT(Stm_Test_Isr, 0, 2)
{
    uint32 late = IfxStm_getLower(&MODULE_STM0) - MODULE_STM0.CMP[0].U;
    IfxStm_Timer_acknowledgeTimerIrq(&timer);
    ++stmInterrupts;
    if (late > stmMaxLateTicks)
        stmMaxLateTicks = late;
    if (late >= (uint32)(stmHz / 1000.0F))
        ++stmLate;
}

static void Pwm_SetStage(void)
{
    uint32 hz = pwmStage / 3 == 0 ? 1000 : (pwmStage / 3 == 1 ? 10000 : 100000);
    pwmPeriodTicks = (uint32)(capture.captureClockFrequency / hz + 0.5F);
    pwmDutyTicks = pwmPeriodTicks * ((pwmStage % 3 + 1) * 25) / 100;
    IfxGtm_Tom_Ch_setCompareZeroShadow(pwm.tom, pwm.tomChannel, pwmPeriodTicks);
    IfxGtm_Tom_Ch_setCompareOneShadow(pwm.tom, pwm.tomChannel, pwmDutyTicks);
    pwmChangedAt = IfxStm_getLower(&MODULE_STM0);
}

static void Pwm_Init(void)
{
    IfxGtm_enable(&MODULE_GTM);
    float32 hz = IfxGtm_Cmu_getModuleFrequency(&MODULE_GTM);
    IfxGtm_Cmu_setGclkFrequency(&MODULE_GTM, hz);
    /* TOM 的 FXCLK1 = GCLK/16，TIM 的 CLK0 同样分频，覆盖 1~100 kHz。 */
    IfxGtm_Cmu_setClkFrequency(&MODULE_GTM, IfxGtm_Cmu_Clk_0, hz / 16.0F);
    IfxGtm_Cmu_enableClocks(&MODULE_GTM, IFXGTM_CMU_CLKEN_FXCLK | IFXGTM_CMU_CLKEN_CLK0);

    IfxGtm_Tim_In_Config input;
    IfxGtm_Tim_In_initConfig(&input, &MODULE_GTM);
    input.filter.inputPin = &IfxGtm_TIM0_6_P20_0_IN;
    input.filter.inputPinMode = IfxPort_InputMode_pullDown;
    input.capture.clock = IfxGtm_Cmu_Clk_0;
    input.capture.mode = Ifx_Pwm_Mode_leftAligned;
    IfxGtm_Tim_In_init(&capture, &input);

    IfxGtm_Tom_Pwm_Config output;
    IfxGtm_Tom_Pwm_initConfig(&output, &MODULE_GTM);
    output.tom = IfxGtm_Tom_1;
    output.tomChannel = IfxGtm_Tom_Ch_12;
    output.clock = IfxGtm_Tom_Ch_ClkSrc_cmuFxclk1;
    output.period = (uint32)(capture.captureClockFrequency / 1000.0F + 0.5F);
    output.dutyCycle = output.period / 4;
    output.synchronousUpdateEnabled = TRUE;
    output.pin.outputPin = &IfxGtm_TOM1_12_TOUT61_P20_3_OUT;
    output.pin.outputMode = IfxPort_OutputMode_pushPull;
    output.pin.padDriver = IfxPort_PadDriver_cmosAutomotiveSpeed1;
    IfxGtm_Tom_Pwm_init(&pwm, &output);
    IfxGtm_Tom_Pwm_start(&pwm, TRUE);
    Pwm_SetStage();
}

static void Pwm_Check(void)
{
    IfxGtm_Tim_In_update(&capture);
    if (capture.dataLost)
        ++pwmSkipped; /* 未读取每个周期，不算波形错误。 */
    if (!capture.newData)
        return;
    if (!capture.dataCoherent)
    {
        ++pwmIncoherent;
        return;
    }
    /* 更新后给最长 1 ms 的 PWM 留两个周期稳定。 */
    if ((uint32)(IfxStm_getLower(&MODULE_STM0) - pwmChangedAt) < (uint32)(stmHz / 500.0F))
        return;
    ++pwmSamples[pwmStage];
    sint32 periodError = (sint32)capture.periodTick - pwmPeriodTicks;
    sint32 dutyError = (sint32)capture.pulseLengthTick - pwmDutyTicks;
    if (periodError < -3 || periodError > 3 || dutyError < -3 || dutyError > 3)
        ++pwmErrors[pwmStage];
    if (capture.periodTick != 0)
    {
        pwmMeasuredHz = capture.captureClockFrequency / capture.periodTick;
        pwmMeasuredDuty = (float32)capture.pulseLengthTick * 100.0F / capture.periodTick;
    }
}

static void Screen_Update(void)
{
    /* 每秒用 QSPI/DMA 刷图，三核计算与定时器同时运行。 */
    Ips200_FillRect(0, 0, 240, 288, IPS200_BLACK);
    Ips200_ShowString(0, 0, "TC377 OVERCLOCK TEST");
    Ips200_ShowString(0, 24, "CPU MHz:");
    Ips200_ShowFloat(80, 24, cpu0Hz / 1000000.0F, 0);
    Ips200_ShowString(0, 48, "TIME s:");
    Ips200_ShowUint(80, 48, uptimeSeconds);
    Ips200_ShowString(0, 72, "PWM Hz:");
    Ips200_ShowFloat(80, 72, pwmMeasuredHz, 0);
    Ips200_ShowString(0, 96, "DUTY %:");
    Ips200_ShowFloat(80, 96, pwmMeasuredDuty, 1);
    Ips200_ShowString(0, 120, "CPU0 ERR:");
    Ips200_ShowUint(80, 120, stressErrors[0]);
    Ips200_ShowString(0, 144, "CPU1 ERR:");
    Ips200_ShowUint(80, 144, stressErrors[1]);
    Ips200_ShowString(0, 168, "CPU2 ERR:");
    Ips200_ShowUint(80, 168, stressErrors[2]);
    Ips200_ShowString(0, 192, "STM LATE:");
    Ips200_ShowUint(80, 192, stmLate);
    uint32 samples = 0, errors = 0;
    for (uint32 i = 0; i < 9; ++i)
    {
        samples += pwmSamples[i];
        errors += pwmErrors[i];
    }
    Ips200_ShowString(0, 216, "PWM ERR:");
    Ips200_ShowUint(80, 216, errors);
    Ips200_ShowString(0, 240, "CAPTURE:");
    Ips200_ShowUint(80, 240, samples);
    Ips200_FillRect(0, 288, 240, 32, (screenFrames & 1) ? IPS200_BLUE : IPS200_GREEN);
    screenPending = TRUE;
}

void core0_main(void)
{
    IfxCpu_enableInterrupts();
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());
    cpu0Hz = IfxScuCcu_getCpuFrequency(IfxCpu_ResourceCpu_0);
    cpu1Hz = IfxScuCcu_getCpuFrequency(IfxCpu_ResourceCpu_1);
    cpu2Hz = IfxScuCcu_getCpuFrequency(IfxCpu_ResourceCpu_2);
    sriHz = IfxScuCcu_getSriFrequency();
    spbHz = IfxScuCcu_getSpbFrequency();
    stmHz = IfxStm_getFrequency(&MODULE_STM0);
    Ips200_Init();
    Platform_Init();
    IfxCpu_emitEvent(&g_cpuSyncEvent);
    while (IfxCpu_waitEvent(&g_cpuSyncEvent, 1)) {}
    Pwm_Init();
    gtmHz = IfxGtm_getSysClkFrequency();
    IfxStm_Timer_Config config;
    IfxStm_Timer_initConfig(&config, &MODULE_STM0);
    config.base.frequency = 1000.0F;
    config.base.minResolution = 1.0F / stmHz; /* CMP0 与 STM 低 32 位使用相同计数位。 */
    config.base.isrPriority = 2;
    config.base.isrProvider = IfxSrc_Tos_cpu0;
    config.comparator = IfxStm_Comparator_0;
    IfxStm_Timer_init(&timer, &config);
    IfxStm_Timer_run(&timer);
    uint32 second = IfxStm_getLower(&MODULE_STM0);
    while (1)
    {
        qspiErrorFlags |= MODULE_QSPI2.STATUS.B.ERRORFLAGS;
        if (screenPending && !Ips200_IsBusy())
        {
            ++screenFrames; /* 完成整批 DMA 后才计数。 */
            screenPending = FALSE;
        }
        Pwm_Check();
        Overclock_Stress(0);
        Platform_CoreStep(0);
        Platform_Step();
        if ((uint32)(IfxStm_getLower(&MODULE_STM0) - second) >= (uint32)stmHz)
        {
            second += (uint32)stmHz;
            ++uptimeSeconds;
            Platform_Second();
            if (!Ips200_IsBusy())
                Screen_Update();
            pwmStage = (pwmStage + 1) % 9;
            Pwm_SetStage();
        }
    }
}
