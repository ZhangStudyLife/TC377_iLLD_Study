#ifndef APP_HARDWARE_IPS200_H
#define APP_HARDWARE_IPS200_H

#include "Ifx_Types.h"

#define IPS200_WIDTH   240U
#define IPS200_HEIGHT  320U

#define IPS200_BLACK   0x0000U
#define IPS200_WHITE   0xFFFFU
#define IPS200_RED     0xF800U
#define IPS200_GREEN   0x07E0U
#define IPS200_BLUE    0x001FU
#define IPS200_YELLOW  0xFFE0U
#define IPS200_CYAN    0x07FFU
#define IPS200_MAGENTA 0xF81FU

/* ST7789V2，固定竖屏 240×320，QSPI2 / Mode 0。
 * 300/500 MHz 档位共用标准外设 PLL，命令与像素均为 50 MHz。
 * 命令使用 8 位帧，全部像素使用完整 RGB565，MSB 优先。
 * 绘图入队后返回，DMA0/1 每请求搬运一帧，接收优先排空。
 * CPU0 优先级 40 中断自动推进；只有 Init 等待复位、黑屏完成。
 * DMA0/1、CPU0 中断优先级 40 与 QSPI2 由本驱动独占。
 * SCK=P15.3，MOSI=P15.5，RST=P15.1，DC=P15.0，CS=P15.2，BL=P15.4；无 MISO。
 * 由 CPU0 主循环单独调用，必须开启中断；先 Init 再绘图。
 * 队列最多容纳 63 个未完成任务，DrawRect 提交 4 个任务，其他绘图提交 1 个。
 * 调用方保证队列有空间；整批任务完成后 IsBusy 返回 FALSE，再提交下一批。
 * 左上角为 (0,0)，颜色为 RGB565；调用方保证完整绘制区域在屏内、
 * 指针有效、宽高非零。文字只接受 ASCII 32～126，不裁剪、不自动换行。
 * 字符串复制入队，最多 30 字符；图像在 IsBusy 返回 FALSE 前必须有效且不修改。
 *
 * 最小示例（系统时钟初始化完成后，在 CPU0 调用）：
 *   Ips200_Init();
 *   Ips200_ShowString(0, 0, "TC377 IPS200");
 *   Ips200_ShowInt(0, 16, -123);
 *   Ips200_ShowFloat(0, 32, 3.14, 2);
 *   // 返回后可以做其他工作，用 Ips200_IsBusy() 查询整批任务是否完成。
 */
void Ips200_Init(void); /* 黑屏后开启背光；默认白色文字、黑色背景 */
boolean Ips200_IsBusy(void); /* TRUE 表示仍有任务或像素正在发送，无需 Service */
extern volatile float32 ips200SpiHz; /* iLLD 计算的实际 SCK（Hz），可在 Expressions 查看 */
extern volatile uint32 ips200IrqTicks; /* STM tick 累计中断服务耗时，不含入口/出口 */
void Ips200_SetColor(uint16 pen, uint16 background);
void Ips200_Clear(void); /* 使用当前背景色 */
void Ips200_Fill(uint16 color);
void Ips200_DrawPoint(uint16 x, uint16 y, uint16 color);
void Ips200_DrawLine(uint16 x0, uint16 y0, uint16 x1, uint16 y1, uint16 color);
void Ips200_DrawRect(uint16 x, uint16 y, uint16 width, uint16 height, uint16 color);
void Ips200_FillRect(uint16 x, uint16 y, uint16 width, uint16 height, uint16 color);

/* 固定 8×16 字体，覆盖背景；较短的新文字不会清除旧文字尾部。 */
void Ips200_ShowChar(uint16 x, uint16 y, char character);
void Ips200_ShowString(uint16 x, uint16 y, const char *text);
void Ips200_ShowInt(uint16 x, uint16 y, sint32 value);
void Ips200_ShowUint(uint16 x, uint16 y, uint32 value);
/* value 为有限值，decimals 为 0～6；调用方保证格式化后的文字能放入屏内。 */
void Ips200_ShowFloat(uint16 x, uint16 y, float64 value, uint8 decimals);
/* 原尺寸图像，按行排列的 width*height 个 RGB565 数值，无需预先交换字节。 */
void Ips200_ShowRgb565Image(uint16 x, uint16 y, const uint16 *image,
                           uint16 width, uint16 height);

#endif
