#include "Key.h"

/* 初始化所有按键: 按 keyTable 配置每个按键引脚的输入模式与 Pad 特性 */
void Key_Init(void)
{
    for (uint8 i = 0; i < KEY_MAX; i++)
    {
        /* 输入模式 (上拉/下拉/无内部电阻), 由 keyTable.inputMode 决定 */
        IfxPort_setPinModeInput(keyTable[i].pin.port, keyTable[i].pin.pinIndex, keyTable[i].inputMode);
        /* 输入 Pad 特性: PL 决定输入电平阈值标准, 应按供电电压选择合适档位 */
        IfxPort_setPinPadDriver(keyTable[i].pin.port, keyTable[i].pin.pinIndex, keyTable[i].padDriver);
    }
}

/* 查询按键是否按下: 内部按 activeLow 把物理电平归一化为逻辑"按下"语义 */
boolean Key_IsPressed(KeyId key)
{
    if (key >= KEY_MAX)
    {
        return FALSE;   /* 越界键按"未按下"处理, 避免访问 keyTable 越界 */
    }

    boolean pinLevel = IfxPort_getPinState(keyTable[key].pin.port, keyTable[key].pin.pinIndex);
    return (pinLevel == (boolean)keyTable[key].activeLow);
}