#include "Led.h"

void Led_Init()
{
    for (uint8 i = 0 ; i<LED_MAX ; i++)
    {
        IfxPort_setPinHigh(ledTable[i].pin.port,ledTable[i].pin.pinIndex);
        IfxPort_setPinModeOutput(ledTable[i].pin.port,ledTable[i].pin.pinIndex,IfxPort_OutputMode_pushPull,IfxPort_OutputIdx_general);
        IfxPort_setPinPadDriver(ledTable[i].pin.port,ledTable[i].pin.pinIndex,IfxPort_PadDriver_cmosAutomotiveSpeed4);
    }
}

void Led_On(LedId led)
{
    IfxPort_setPinLow(ledTable[led].pin.port,ledTable[led].pin.pinIndex);
}

void Led_Off(LedId led)
{
    IfxPort_setPinHigh(ledTable[led].pin.port,ledTable[led].pin.pinIndex);
}

void Led_Toggle(LedId led)
{
    IfxPort_togglePin(ledTable[led].pin.port,ledTable[led].pin.pinIndex);
}
