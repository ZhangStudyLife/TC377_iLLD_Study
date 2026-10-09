#ifndef APP_HARDWARE_LED_H
#define APP_HARDWARE_LED_H

#include "Ifx_Types.h"
#include "Port/Std/IfxPort.h"

typedef enum
{
    LED_0 = 0,   /* 对应板子第 1 个 LED */
    LED_1,
    LED_2,
    LED_3,
    LED_MAX
} LedId;         /* 枚举名以 LED_ 开头，避免和全局命名冲突 */

typedef struct
{
    IfxPort_Pin        pin;          /* 复用 iLLD 官方引脚结构体: {port, pinIndex} */
    boolean            activeHigh;   /* 灯是"高电平点亮"还是"低电平点亮" */
    IfxPort_PadDriver  padDriver;    /* 每个灯可单独设驱动能力 */
} LedConfig;

static const LedConfig ledTable[LED_MAX] =
{
    [LED_0] = { { &MODULE_P20, 9 }, TRUE,  IfxPort_PadDriver_cmosAutomotiveSpeed4 },
    [LED_1] = { { &MODULE_P20, 8 }, TRUE,  IfxPort_PadDriver_cmosAutomotiveSpeed4 },
    [LED_2] = { { &MODULE_P21, 5 }, TRUE,  IfxPort_PadDriver_cmosAutomotiveSpeed4 },
    [LED_3] = { { &MODULE_P21, 4 }, TRUE,  IfxPort_PadDriver_cmosAutomotiveSpeed4 },
};

void Led_Init();
void Led_On(LedId led);
void Led_Off(LedId led);
void Led_Toggle(LedId led);

#endif
