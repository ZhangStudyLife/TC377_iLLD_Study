#ifndef PLATFORM_TEST_H
#define PLATFORM_TEST_H
#include "Ifx_Types.h"

extern volatile uint32 ipcPackets[3], ipcErrors[3], ipcInterrupts[3], atomicChecks[3];
extern volatile uint32 uartBytes, uartErrors, smuInterrupts, smuErrors;
extern volatile uint32 watchdogServices[4], faultCommand, bootReset, bootAlarms[12], alarmSeen[12];
extern volatile uint32 platformReady, mbistPassed, mbistErrors, mbistStatus, mbistAddress;
extern volatile uint32 sshBoot[37], sshSeen[37];
extern volatile float32 treeHz[13], dieTemperature, peakTemperature;
extern volatile uint32 ccuInterrupts, ccuErrors, dmaChecks, dmaErrors, crcChecks, crcErrors, canFrames[4], canErrors;
extern volatile uint32 gptChecks, gptErrors;

void Platform_Init(void);
void Platform_CoreInit(uint32 core);
void Platform_CoreStep(uint32 core);
void Platform_Step(void);
void Platform_Second(void);
#endif
