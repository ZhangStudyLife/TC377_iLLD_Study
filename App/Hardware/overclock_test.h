#ifndef OVERCLOCK_TEST_H
#define OVERCLOCK_TEST_H

#include "Ifx_Types.h"

extern volatile uint32 stressLoops[3], stressErrors[3];

/* 三个核分别执行整数、浮点和 RAM 校验，数组各自独立。
 * 这只验证当前负载下的计算结果，不代表芯片全部指令/存储器已覆盖。
 */
static void Overclock_Stress(uint32 core)
{
    volatile uint32 ram[256]; /* 放在各核自己的 DSPR 栈，避免三核争用 CPU0 RAM。 */
    volatile uint32 value = 0x12345678U;
    volatile float32 f = 1.5F;
    for (uint32 i = 0; i < 256; ++i)
    {
        value = value * 1664525U + 1013904223U;
        f = f * 0.5F + 1.5F;
        ram[i] = 0xA5A5A5A5U ^ (i * 0x10204081U);
    }
    if (value != 0x99C2DB78U || f != 3.0F)
        ++stressErrors[core];
    for (uint32 i = 0; i < 256; ++i)
    {
        if (ram[i] != (0xA5A5A5A5U ^ (i * 0x10204081U)))
            ++stressErrors[core];
        ram[i] = ~ram[i];
        if (ram[i] != ~(0xA5A5A5A5U ^ (i * 0x10204081U)))
            ++stressErrors[core];
    }
    ++stressLoops[core];
}

#endif
