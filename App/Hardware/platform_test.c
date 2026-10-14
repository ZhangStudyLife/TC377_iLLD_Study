#include "platform_test.h"
#include "IfxCpu.h"
#include "IfxCpu_Irq.h"
#include "IfxScuWdt.h"
#include "IfxScuCcu.h"
#include "IfxSrc.h"
#include "IfxSmu.h"
#include "IfxAsclin_Asc.h"
#include "IfxMtu.h"
#include "IfxDts_Dts.h"
#include "IfxStm.h"
#include "IfxCcu6_Timer.h"
#include "IfxDma_Dma.h"
#include "IfxFce_Crc.h"
#include "IfxCan_Can.h"
#include "IfxGpt12.h"
#include "IfxCbs_reg.h"

volatile uint32 ipcPackets[3], ipcErrors[3], ipcInterrupts[3], atomicChecks[3];
volatile uint32 uartBytes, uartErrors, smuInterrupts, smuErrors;
volatile uint32 watchdogServices[4], faultCommand, bootReset, bootAlarms[12], alarmSeen[12];
volatile uint32 platformReady, mbistPassed, mbistErrors, mbistStatus, mbistAddress;
volatile uint32 sshBoot[37], sshSeen[37];
/* MC40 的 FSI RAM 源标志清除依据 TC37x 勘误 SMU_TC.H012。 */
static const uint8 ssh[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,34,35,38,41,42,53,55,57,58,59,60,62,63,65,66,68,70,77,78,82,83,40};
extern IfxCpu_syncEvent g_cpuSyncEvent;
volatile float32 treeHz[13], dieTemperature, peakTemperature;
volatile uint32 ccuInterrupts, ccuErrors, dmaChecks, dmaErrors, crcChecks, crcErrors, canFrames[4], canErrors;
volatile uint32 gptChecks, gptErrors;
extern volatile uint32 stressLoops[3];

typedef struct { uint32 sequence, ack, data[16], checksum, reply; } Mailbox;
typedef struct { Mailbox box[3][3]; uint32 lock, count, inverse; } Shared;
static Shared shared __attribute__((section(".lmubss"), aligned(32)));
/* DLMU 的非缓存镜像。volatile 和 dsync 不能代替缓存一致性处理。 */
#define IPC ((volatile Shared *)((uint32)&shared | 0x20000000U))
static uint32 lastService[3], progress[3];
static IfxAsclin_Asc uart;
static uint8 txBuffer[256 + sizeof(Ifx_Fifo) + 8], rxBuffer[256 + sizeof(Ifx_Fifo) + 8];
static IfxCcu6_Timer ccu;
static IfxDma_Dma_Channel dma;
static uint32 source[64] __attribute__((aligned(32))), destination[64] __attribute__((aligned(32)));
static IfxFce_Crc_Crc crc;
static IfxCan_Can_Node canNode[2];

IFX_INTERRUPT(Ipc0_Isr, 0, 12) { ++ipcInterrupts[0]; }
IFX_INTERRUPT(Ipc1_Isr, 1, 12) { ++ipcInterrupts[1]; }
IFX_INTERRUPT(Ipc2_Isr, 2, 12) { ++ipcInterrupts[2]; }
IFX_INTERRUPT(Ccu_Isr, 0, 10)
{
    static uint32 previous;
    uint32 now = IfxStm_getLower(&MODULE_STM0);
    uint32 expected = (uint32)(IfxStm_getFrequency(&MODULE_STM0) / 100.0F);
    if (previous && ((uint32)(now - previous) < expected * 9 / 10 || (uint32)(now - previous) > expected * 11 / 10)) ++ccuErrors;
    previous = now;
    ++ccuInterrupts;
    IfxCcu6_clearInterruptStatusFlag(&MODULE_CCU60, IfxCcu6_InterruptSource_t12PeriodMatch);
}
IFX_INTERRUPT(Uart_TxIsr, 0, 18) { IfxAsclin_Asc_isrTransmit(&uart); }
IFX_INTERRUPT(Uart_RxIsr, 0, 19) { IfxAsclin_Asc_isrReceive(&uart); }
IFX_INTERRUPT(Uart_ErrorIsr, 0, 20) { ++uartErrors; IfxAsclin_Asc_isrError(&uart); }
IFX_INTERRUPT(Smu_Isr, 0, 22)
{
    ++smuInterrupts;
    if (!IfxSmu_getAlarmStatus(IfxSmu_Alarm_Software_Alarm0)) ++smuErrors;
    IfxSmu_clearAlarmStatus(IfxSmu_Alarm_Software_Alarm0);
    IfxSmu_clearAlarmExecutedStatus(IfxSmu_AlarmExecutionStatus_irq0);
}

void Platform_CoreInit(uint32 core)
{
    IfxScuWdt_Config config;
    IfxScuWdt_initConfig(&config);
    config.reload = 0xFC00;
    config.disableWatchdog = FALSE;
    config.inputFrequency = IfxScu_WDTCON1_IR_divBy16384;
    IfxScuWdt_initCpuWatchdog(&MODULE_SCU.WDTCPU[core], &config);
    lastService[core] = IfxStm_getLower(&MODULE_STM0);
}

void Platform_CoreStep(uint32 core)
{
    /* 三核全互通：每个方向独占邮箱，先发布数据，再发布序号，再中断通知。 */
    for (uint32 peer = 0; peer < 3; ++peer)
    {
        if (peer == core) continue;
        volatile Mailbox *in = &IPC->box[peer][core];
        if (in->sequence != in->ack)
        {
            uint32 sequence = in->sequence, checksum = 0;
            for (uint32 i = 0; i < 16; ++i)
            {
                uint32 value = in->data[i];
                if (value != (sequence ^ (i * 0x10204081U))) ++ipcErrors[core];
                checksum ^= value;
            }
            if (checksum != in->checksum) ++ipcErrors[core];
            in->reply = checksum;
            __dsync();
            in->ack = sequence;
            ++ipcPackets[core];
        }
        volatile Mailbox *out = &IPC->box[core][peer];
        if (out->sequence == out->ack)
        {
            if (out->sequence && out->reply != out->checksum) ++ipcErrors[core];
            uint32 sequence = out->sequence + 1, checksum = 0;
            for (uint32 i = 0; i < 16; ++i)
            {
                uint32 value = sequence ^ (i * 0x10204081U);
                out->data[i] = value;
                checksum ^= value;
            }
            out->checksum = checksum;
            __dsync();
            out->sequence = sequence;
            __dsync();
            IfxSrc_setRequest(&MODULE_SRC.GPSR.GPSR[0].SR[peer]);
        }
    }
    /* 在锁内检查共享计数与反码，再由三核共同递增。 */
    if (Ifx__cmpAndSwap((volatile unsigned int *)&IPC->lock, 1, 0) == 0)
    {
        if (IPC->inverse != ~IPC->count) ++ipcErrors[core];
        ++IPC->count;
        IPC->inverse = ~IPC->count;
        ++atomicChecks[core];
        __dsync();
        IPC->lock = 0;
    }
    uint32 now = IfxStm_getLower(&MODULE_STM0);
    if ((uint32)(now - lastService[core]) >= (uint32)(IfxStm_getFrequency(&MODULE_STM0) / 1000.0F))
    {
        lastService[core] = now;
        if (faultCommand != core + 1) /* 1/2/3：故意停止对应 CPU WDT 喂狗。 */
        {
            IfxScuWdt_serviceCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
            ++watchdogServices[core];
        }
    }
}

void Platform_Init(void)
{
    /* 先在全部 WDT 禁用时解除调试挂起，再配置并启动计时。 */
    MODULE_CBS.OCNTRL.U = 0x3000;
    treeHz[0] = IfxScuCcu_getBbbFrequency();
    treeHz[1] = IfxScuCcu_getPerPllFrequency1();
    treeHz[2] = IfxScuCcu_getPerPllFrequency2();
    treeHz[3] = IfxScuCcu_getQspiFrequency();
    treeHz[4] = IfxScuCcu_getI2cFrequency();
    treeHz[5] = IfxScuCcu_getAsclinSFrequency();
    treeHz[6] = IfxScuCcu_getAsclinFFrequency();
    treeHz[7] = IfxScuCcu_getMcanFrequency();
    treeHz[8] = IfxScuCcu_getMcanhFrequency();
    treeHz[9] = IfxScuCcu_getGethFrequency();
    treeHz[10] = IfxScuCcu_getFsiFrequency();
    treeHz[11] = IfxScuCcu_getFsi2Frequency();
    treeHz[12] = IfxScuCcu_getAdcFrequency();
    bootReset = SCU_RSTSTAT.U;
    for (uint32 group = 0; group < 12; ++group) bootAlarms[group] = MODULE_SMU.AG[group].U;
    for (uint32 i = 0; i < sizeof(shared) / 4; ++i) ((volatile uint32 *)IPC)[i] = 0;
    IPC->inverse = ~0U;
    __dsync();
    for (uint32 core = 0; core < 3; ++core)
    {
        IfxSrc_init(&MODULE_SRC.GPSR.GPSR[0].SR[core], IfxCpu_Irq_getTos(core), 12);
        IfxSrc_enable(&MODULE_SRC.GPSR.GPSR[0].SR[core]);
    }

    /* CPU0 从 Flash 执行，测试前关闭与 PSPR 共用 SSH 的程序缓存。 */
    IfxCpu_setProgramCache(FALSE);
    while ((g_cpuSyncEvent & 6) != 6) {}
    IfxMtu_enableModule();
    uint16 password = IfxScuWdt_getSafetyWatchdogPassword();
    IfxScuWdt_clearSafetyEndinit(password);
    /* 保留 SSH 启动诊断，再清源标志；不关闭 ECC 检测或报警通知。 */
    for (uint32 i = 0; i < sizeof(ssh); ++i)
    {
        sshBoot[i] = MODULE_MTU.MC[ssh[i]].ECCD.U | ((uint32)MODULE_MTU.MC[ssh[i]].FAULTSTS.U << 16);
        MODULE_MTU.MC[ssh[i]].FAULTSTS.U = 0;
        MODULE_MTU.MC[ssh[i]].ECCD.U = 0;
    }
    IfxScuWdt_setSafetyEndinit(password);
    const IfxMtu_MbistSel memory[] = {IfxMtu_MbistSel_cpu0Pspr, IfxMtu_MbistSel_cpu1Pspr, IfxMtu_MbistSel_cpu2Pspr};
    for (uint32 i = 0; i < 3; ++i)
    {
        IfxMtu_clearSram(memory[i]);
        MODULE_MTU.MC[memory[i]].ECCD.U = 0x10; /* 清启动遗留错误与跟踪状态。 */
        uint16 errorAddress = 0;
        if (IfxMtu_runNonDestructiveInversionTest(memory[i], 0, 0, 0, &errorAddress)) ++mbistErrors;
        else ++mbistPassed;
        mbistAddress |= errorAddress;
        mbistStatus |= MODULE_MTU.MC[memory[i]].ECCD.U;
    }
    IfxCpu_invalidateProgramCache();
    IfxCpu_setProgramCache(TRUE);
    IfxScuWdt_clearSafetyEndinit(password);
    for (uint32 i = 0; i < 3; ++i)
    {
        MODULE_MTU.MC[memory[i]].FAULTSTS.U = 0; /* MBIST 自身会置 OPERR。 */
        MODULE_MTU.MC[memory[i]].ECCD.U = 0;
    }
    IfxScuWdt_setSafetyEndinit(password);
    IfxDts_Dts_Config dts;
    IfxDts_Dts_initModuleConfig(&dts);
    IfxDts_Dts_initModule(&dts);

    IfxAsclin_Asc_Config config;
    IfxAsclin_Asc_initModuleConfig(&config, &MODULE_ASCLIN0);
    config.baudrate.baudrate = 115200;
    config.baudrate.prescaler = 4;
    config.baudrate.oversampling = IfxAsclin_OversamplingFactor_16;
    config.bitTiming.samplePointPosition = IfxAsclin_SamplePointPosition_8;
    config.bitTiming.medianFilter = IfxAsclin_SamplesPerBit_three;
    config.interrupt.txPriority = 18;
    config.interrupt.rxPriority = 19;
    config.interrupt.erPriority = 20;
    config.interrupt.typeOfService = IfxSrc_Tos_cpu0;
    config.txBuffer = txBuffer; config.txBufferSize = 256;
    config.rxBuffer = rxBuffer; config.rxBufferSize = 256;
    const IfxAsclin_Asc_Pins pins = {NULL_PTR, IfxPort_InputMode_pullUp,
        &IfxAsclin0_RXA_P14_1_IN, IfxPort_InputMode_pullUp, NULL_PTR, IfxPort_OutputMode_pushPull,
        &IfxAsclin0_TX_P14_0_OUT, IfxPort_OutputMode_pushPull, IfxPort_PadDriver_cmosAutomotiveSpeed1};
    config.pins = &pins;
    IfxAsclin_Asc_initModule(&uart, &config);

    IfxCcu6_Timer_Config timer;
    IfxCcu6_Timer_initModuleConfig(&timer, &MODULE_CCU60);
    timer.base.t12Frequency = IfxScuCcu_getSpbFrequency() / 128;
    timer.base.t12Period = (uint32)(timer.base.t12Frequency / 100 + 0.5F);
    timer.timer = IfxCcu6_TimerId_t12;
    timer.interrupt1.source = IfxCcu6_InterruptSource_t12PeriodMatch;
    timer.interrupt1.serviceRequest = IfxCcu6_ServiceRequest_0;
    timer.interrupt1.priority = 10;
    IfxCcu6_Timer_initModule(&ccu, &timer);
    IfxCcu6_Timer_start(&ccu);
    IfxGpt12_enableModule(&MODULE_GPT120);
    IfxGpt12_setGpt1BlockPrescaler(&MODULE_GPT120, IfxGpt12_Gpt1BlockPrescaler_32);
    IfxGpt12_T3_setMode(&MODULE_GPT120, IfxGpt12_Mode_timer);
    IfxGpt12_T3_setTimerPrescaler(&MODULE_GPT120, IfxGpt12_TimerInputPrescaler_128);
    IfxGpt12_T3_setTimerDirection(&MODULE_GPT120, IfxGpt12_TimerDirection_up);
    IfxGpt12_T3_run(&MODULE_GPT120, IfxGpt12_TimerRun_start);

    IfxDma_Dma module;
    IfxDma_Dma_Config dc;
    IfxDma_Dma_initModuleConfig(&dc, &MODULE_DMA);
    IfxDma_Dma_initModule(&module, &dc);
    IfxDma_Dma_ChannelConfig channel;
    IfxDma_Dma_initChannelConfig(&channel, &module);
    channel.channelId = IfxDma_ChannelId_2;
    channel.sourceAddress = (uint32)source;
    channel.destinationAddress = (uint32)destination;
    channel.transferCount = 64;
    channel.moveSize = IfxDma_ChannelMoveSize_32bit;
    channel.requestMode = IfxDma_ChannelRequestMode_completeTransactionPerRequest;
    channel.operationMode = IfxDma_ChannelOperationMode_continuous;
    IfxDma_Dma_initChannel(&dma, &channel);

    IfxFce_Crc fce;
    IfxFce_Crc_Config fc;
    IfxFce_Crc_initModuleConfig(&fc, &MODULE_FCE);
    IfxFce_Crc_initModule(&fce, &fc);
    IfxFce_Crc_CrcConfig kernel;
    IfxFce_Crc_initCrcConfig(&kernel, &fce);
    kernel.crcCheckCompared = FALSE;
    kernel.dataByteReflectionEnabled = FALSE;
    kernel.crc32BitReflectionEnabled = FALSE;
    kernel.crcResultInverted = FALSE;
    IfxFce_Crc_initCrc(&crc, &kernel);

    IfxCan_Can can;
    IfxCan_Can_Config cc;
    IfxCan_Can_initModuleConfig(&cc, &MODULE_CAN0);
    IfxCan_Can_initModule(&can, &cc);
    for (uint32 i = 0; i < 2; ++i)
    {
        IfxCan_Can_NodeConfig node;
        IfxCan_Can_initNodeConfig(&node, &can);
        node.nodeId = (IfxCan_NodeId)i;
        node.busLoopbackEnabled = TRUE;
        node.frame.type = IfxCan_FrameType_transmitAndReceive;
        node.frame.mode = IfxCan_FrameMode_fdLongAndFast;
        node.fastBaudRate.baudrate = 2000000;
        node.txConfig.txBufferDataFieldSize = IfxCan_DataFieldSize_64;
        node.rxConfig.rxBufferDataFieldSize = IfxCan_DataFieldSize_64;
        node.filterConfig.standardListSize = 1;
        node.messageRAM.standardFilterListStartAddress += i * 0x1000;
        node.messageRAM.txBuffersStartAddress += i * 0x1000;
        node.messageRAM.rxBuffersStartAddress += i * 0x1000;
        IfxCan_Can_initNode(&canNode[i], &node);
        IfxCan_Filter filter = {0};
        filter.elementConfiguration = IfxCan_FilterElementConfiguration_storeInRxBuffer;
        filter.id1 = 0x123 + (1 - i);
        filter.rxBufferOffset = IfxCan_RxBufferId_0;
        IfxCan_Can_setStandardFilter(&canNode[i], &filter);
    }

    IfxSmu_unlockConfigRegisters();
    IfxScuWdt_clearSafetyEndinit(password);
    MODULE_SMU.AGC.B.IGCS0 = 1;
    MODULE_SCU.RSTCON.B.SMU = 1; /* 全系统复位，重新初始化三核与外设。 */
    IfxScuWdt_setSafetyEndinit(password);
    IfxSmu_setAlarmAction(IfxSmu_Alarm_Software_Alarm0, IfxSmu_InternalAlarmAction_igcs0);
    /* Watchdog 到期必须有可验证的硬件复位反应。 */
    const IfxSmu_Alarm wd[] = {IfxSmu_Alarm_SCU_CPU0_WatchdogTimeOut, IfxSmu_Alarm_SCU_CPU1_WatchdogTimeOut,
        IfxSmu_Alarm_SCU_CPU2_WatchdogTimeOut, IfxSmu_Alarm_SCU_Safety_WatchdogTimeOut};
    for (uint32 i = 0; i < 4; ++i)
    {
        IfxSmu_clearAlarmStatus(wd[i]);
        IfxSmu_setAlarmAction(wd[i], IfxSmu_InternalAlarmAction_reset);
    }
    /* bootAlarms 保留启动快照；随后单独观察本次运行新增的报警。 */
    for (uint32 group = 0; group < 12; ++group)
    {
        uint32 alarms = MODULE_SMU.AG[group].U;
        for (uint32 bit = 0; bit < 32; ++bit)
            if (alarms & (1U << bit)) IfxSmu_clearAlarmStatus((IfxSmu_Alarm)(group * 32 + bit));
    }
    IfxSmu_lockConfigRegisters();
    IfxSrc_init(IfxSmu_getSrcPointer(IfxSmu_InterruptRequest_0), IfxSrc_Tos_cpu0, 22);
    IfxSrc_enable(IfxSmu_getSrcPointer(IfxSmu_InterruptRequest_0));
    IfxSmu_activateRunState();
    IfxScuWdt_Config wdt;
    IfxScuWdt_initConfig(&wdt);
    wdt.reload = 0xFC00;
    wdt.disableWatchdog = FALSE;
    wdt.clrInternalResetFlag = TRUE;
    wdt.inputFrequency = IfxScu_WDTCON1_IR_divBy16384;
    IfxScuWdt_initSafetyWatchdog(&MODULE_SCU.WDTS, &wdt);
    Platform_CoreInit(0);
    platformReady = 1;
}

void Platform_Step(void)
{
    /* P14.1 收到的字节由主循环送回 P14.0；电脑逐块校验。 */
    uint8 data[32];
    Ifx_SizeT count = 32;
    IfxAsclin_Asc_read(&uart, data, &count, 0);
    if (count)
    {
        uartBytes += count;
        IfxAsclin_Asc_write(&uart, data, &count, 0);
    }
    uint32 now = IfxStm_getLower(&MODULE_STM0);
    static uint32 last;
    if ((uint32)(now - last) >= (uint32)(IfxStm_getFrequency(&MODULE_STM0) / 1000.0F))
    {
        last = now;
        if (faultCommand != 4 && stressLoops[0] != progress[0] && stressLoops[1] != progress[1] && stressLoops[2] != progress[2])
        {
            for (uint32 i = 0; i < 3; ++i) progress[i] = stressLoops[i];
            IfxScuWdt_serviceSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());
            ++watchdogServices[3];
        }
    }
}

void Platform_Second(void)
{
    static uint32 previousStm;
    static uint16 previousGpt;
    uint32 currentStm = IfxStm_getLower(&MODULE_STM0);
    uint16 currentGpt = IfxGpt12_T3_getTimerValue(&MODULE_GPT120);
    if (previousStm)
    {
        uint32 expected = (uint32)((currentStm - previousStm) * (IfxGpt12_T3_getFrequency(&MODULE_GPT120) / IfxStm_getFrequency(&MODULE_STM0)));
        uint32 actual = (uint16)(currentGpt - previousGpt);
        if (actual + 4 < expected || actual > expected + 4) ++gptErrors;
        ++gptChecks;
    }
    previousStm = currentStm;
    previousGpt = currentGpt;
    uint32 softwareCrc = 0;
    for (uint32 i = 0; i < 64; ++i)
    {
        source[i] = (dmaChecks + 1) ^ (i * 0x10204081U);
        destination[i] = ~source[i];
        softwareCrc ^= source[i];
        for (uint32 bit = 0; bit < 32; ++bit)
            softwareCrc = (softwareCrc << 1) ^ ((softwareCrc & 0x80000000U) ? 0x04C11DB7U : 0);
    }
    __dsync();
    IfxDma_Dma_setChannelSourceAddress(&dma, (uint32)source);
    IfxDma_Dma_setChannelDestinationAddress(&dma, (uint32)destination);
    IfxDma_Dma_setChannelTransferCount(&dma, 64);
    IfxDma_Dma_startChannelTransaction(&dma);
    uint32 start = IfxStm_getLower(&MODULE_STM0);
    while (IfxDma_Dma_isChannelTransactionPending(&dma) && (uint32)(IfxStm_getLower(&MODULE_STM0) - start) < (uint32)(IfxStm_getFrequency(&MODULE_STM0) / 1000)) {}
    __dsync();
    for (uint32 i = 0; i < 64; ++i) if (source[i] != destination[i]) ++dmaErrors;
    ++dmaChecks;
    if (IfxFce_Crc_calculateCrc(&crc, source, 64, 0) != softwareCrc) ++crcErrors;
    ++crcChecks;

    /* CAN0 的两节点仅使用内部虚拟总线。 */
    for (uint32 test = 0; test < 4; ++test)
    {
        uint32 sender = test % 2, receiver = 1 - sender;
        uint32 words = test < 2 ? 2 : 16;
        uint32 data[16], received[16] = {0};
        for (uint32 i = 0; i < words; ++i) data[i] = dmaChecks ^ test ^ (i * 0x10204081U);
        IfxCan_Message tx, rx;
        IfxCan_Can_initMessage(&tx);
        IfxCan_Can_initMessage(&rx);
        tx.messageId = 0x123 + sender;
        tx.frameMode = test < 2 ? IfxCan_FrameMode_standard : IfxCan_FrameMode_fdLongAndFast;
        tx.dataLengthCode = test < 2 ? IfxCan_DataLengthCode_8 : IfxCan_DataLengthCode_64;
        if (IfxCan_Can_sendMessage(&canNode[sender], &tx, data) != IfxCan_Status_ok) ++canErrors;
        start = IfxStm_getLower(&MODULE_STM0);
        while (!IfxCan_Can_isNewDataReceived(&canNode[receiver], IfxCan_RxBufferId_0) && (uint32)(IfxStm_getLower(&MODULE_STM0) - start) < (uint32)(IfxStm_getFrequency(&MODULE_STM0) / 1000)) {}
        if (IfxCan_Can_isNewDataReceived(&canNode[receiver], IfxCan_RxBufferId_0))
        {
            IfxCan_Can_readMessage(&canNode[receiver], &rx, received);
            if (rx.messageId != tx.messageId || rx.dataLengthCode != tx.dataLengthCode || rx.frameMode != tx.frameMode) ++canErrors;
            for (uint32 i = 0; i < words; ++i) if (data[i] != received[i]) ++canErrors;
            ++canFrames[test];
        }
        else ++canErrors;
    }
    IfxSmu_setAlarmStatus(IfxSmu_Alarm_Software_Alarm0);
    for (uint32 group = 0; group < 12; ++group) alarmSeen[group] |= MODULE_SMU.AG[group].U;
    for (uint32 i = 0; i < sizeof(ssh); ++i)
        sshSeen[i] |= MODULE_MTU.MC[ssh[i]].ECCD.U | ((uint32)MODULE_MTU.MC[ssh[i]].FAULTSTS.U << 16);
    dieTemperature = IfxDts_Dts_getTemperatureCelsius();
    if (dieTemperature > peakTemperature) peakTemperature = dieTemperature;
}
