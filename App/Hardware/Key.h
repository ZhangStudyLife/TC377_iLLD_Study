#ifndef APP_HARDWARE_KEY_H
#define APP_HARDWARE_KEY_H

#include "Ifx_Types.h"
#include "Port/Std/IfxPort.h"

/* 按键 ID 枚举: 用 KEY_ 前缀, 避免和 LED_ 等全局命名冲突 */
typedef enum
{
    KEY_0 = 0,   /* 板子上第 1 个按键, 对应 P20.6 */
    KEY_1,       /* P20.7 */
    KEY_2,       /* P11.2 */
    KEY_3,       /* P11.3 */
    KEY_MAX      /* 按键数量, 兼作数组边界, 不可作合法 KEY 使用 */
} KeyId;

/* 按键配置结构体: 一个按键的所有板级属性集中放在一个表项里 */
typedef struct
{
    IfxPort_Pin        pin;         /* 复用 iLLD 官方引脚结构体: {port, pinIndex} */
    IfxPort_InputMode  inputMode;   /* 输入模式: 上拉/下拉/无内部电阻 (按板子硬件接法选) */
    boolean            activeLow;   /* TRUE = 按下时引脚为低电平; FALSE = 按下时引脚为高电平 */
    IfxPort_PadDriver  padDriver;   /* 输入 Pad 特性: 决定输入电平阈值标准 (PL) 与驱动 (PD) */
} KeyConfig;

/* 板级按键配置表: 引脚、输入模式、按下电平、PadDriver 集中定义, 改硬件只改这里 */
static const KeyConfig keyTable[KEY_MAX] =
{
    [KEY_0] = { { &MODULE_P20, 6 }, IfxPort_InputMode_pullUp, TRUE, IfxPort_PadDriver_cmosAutomotiveSpeed4 },
    [KEY_1] = { { &MODULE_P20, 7 }, IfxPort_InputMode_pullUp, TRUE, IfxPort_PadDriver_cmosAutomotiveSpeed4 },
    [KEY_2] = { { &MODULE_P11, 2 }, IfxPort_InputMode_pullUp, TRUE, IfxPort_PadDriver_cmosAutomotiveSpeed4 },
    [KEY_3] = { { &MODULE_P11, 3 }, IfxPort_InputMode_pullUp, TRUE, IfxPort_PadDriver_cmosAutomotiveSpeed4 },
};

void Key_Init(void);
boolean Key_IsPressed(KeyId key);

#endif
