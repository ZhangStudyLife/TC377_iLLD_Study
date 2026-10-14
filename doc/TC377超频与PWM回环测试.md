> 历史 520 MHz 试验记录；已被 500 MHz 配置替代，当前配置请看 TC377-500MHz验证.md。

# TC377 超频与 PWM 回环测试

2026-10-14，在当前 TC37x 实物上通过 DAS 调试器烧录并读取运行结果。
接线最终为 **P20.3 → P20.0**；按用户要求跳过 ADC。

## 当前配置

- 三核 CPU 与 SRI：520 MHz，原配置为 300 MHz，目标频率提高约 73.3%。
- 20 MHz 晶振，系统 PLL：PDIV=0、NDIV=51、最终 K2DIV=1。
  DCO 目标 1040 MHz，输出 520 MHz；先以 K2 /8 启动，再经 /4、/3、/2 升频。
- `IFX_CFG_SCU_PLL_FREQUENCY=300000000` 保留为 iLLD 的配置标签；实际倍频由
  `Configurations/Ifx_Cfg.h` 中的 `TC377_CPU_MHZ` 和覆盖宏决定。
- SPB=130 MHz，STM=65 MHz，GTM=260 MHz。STM 与 SPB 保持 1:2。
- 外设 PLL 保留此前的 PLL1=253.33 MHz、PLL2=475 MHz 参数。
- 升频前设置 PFlash RFLASH=15、RECC=3，其余位保留。
- 屏幕命令约 47.5 MHz、像素 59.375 MHz，TX/RX DMA 每次搬运一帧。

频率来自寄存器及工程填写的晶振频率计算，没有用外部频率计测量。
本次采用原供电，未调节核心电压。

## 并发负载与判断方法

1. CPU0/1/2 分别持续执行 256 步整数递推、浮点运算、1 KiB RAM 写入/反码校验。
   RAM 数组放在各核本地 DSPR 栈。整数结果与独立算出的 `0x99C2DB78` 比较。
   `stressLoops[3]` 持续增长，`stressErrors[3]` 应为零。
2. TOM1 CH12 从 P20.3 输出 PWM，TIM0 CH6 从 P20.0 读取实际引脚信号。
   循环测试标称 1/10/100 kHz 和 25%/50%/75% 占空比，共九组。
   周期与高电平长度各允许 ±3 个捕获 tick；切换后跳过前 2 ms。
   九组 `pwmSamples` 均应增加，`pwmErrors` 应为零。
3. STM0 CMP0 每毫秒中断。`stmInterrupts` 应随时间增加约 1000 次/秒。
   ISR 到达延迟达到一个中断周期才累计 `stmLate`；另记录最大延迟。
4. 每秒提交屏幕背景、文字和色条。**整批 DMA 完成后**累计 `screenFrames`。
   同时累计 `qspiErrorFlags`；队列停住或出现 FIFO 错误不能算通过。

高频 PWM 存在整数计数取整。例如标称 100 kHz 不一定恰好为 100000 Hz。
TIM 与 TOM 共享时钟来源，这验证了接线、计数和占空比的一致性，不能代替
示波器对绝对频率、边沿质量或每个周期的测量。`pwmSkipped` 是未读取的周期，
`pwmIncoherent` 是跨周期读取而丢弃的快照，均不作为已验证波形。

## 试验记录

| 配置 | 观察结果 |
|---|---|
| 400 MHz，STM/SPB 非整数比例 | STM 连续中断异常；已修正分频关系 |
| 400 MHz，SPB 200 MHz | 发生异常，未作为最终配置 |
| 300 MHz，原双帧 DMA，并发负载 | 屏幕队列停住；并非只在 CPU 超频时出现 |
| 480 MHz，单帧 DMA，SPI 59.375 MHz | 约 48 秒：九组 PWM、三核校验、STM、48 批屏幕 DMA 均通过 |
| 540 MHz，RFLASH=11、RECC=2 | 约 26 秒后停止，CPU0 Bus Trap/TIN=5（程序存储器完整性错误） |
| 540 MHz，RFLASH=15、RECC=3 | 约 109 秒后停止；不能作为持续通过的结果 |
| 560 MHz，另改 FSI2/BBB 分频的试验 | 未正常启动；该分频试验已撤销，不据此断言所有 560 MHz 配置都失败 |
| 580 MHz，RFLASH=15、RECC=3 | 启动阶段发生异常，未通过 |
| 600 MHz，RFLASH=11、RECC=2 | 未正常进入测试循环，未通过 |
| 520 MHz，RFLASH=15、RECC=3 | 连续 306 秒通过，最终保留此配置 |

上述失败仅描述实测配置，不能作为芯片在其它供电、温度或代码下的极限。
测试界面显示、TIME 数字和蓝绿色条变化已由用户观察确认。

### 最终 520 MHz 数据

快照时间：2026-10-14 11:24:18（Asia/Shanghai），CPU0 处于运行状态。
完整快照保留在 `TC377超频520MHz测试结果.json`。

| 项目 | 结果 |
|---|---|
| 连续运行 | 306 秒 |
| 三核校验轮数 | CPU0=12,167,863；CPU1=12,637,153；CPU2=12,638,839 |
| 三核校验错误 | 0 / 0 / 0 |
| PWM 有效快照合计 | 5,015,330，九组均有采样 |
| PWM 周期/高电平长度检查错误 | 九组均为 0 |
| STM 1 ms 中断次数 | 306,062 |
| STM 延迟超过一个周期 | 0 次 |
| STM 最大到达延迟 | 634 tick / 65 MHz ≈ 9.75 μs |
| 完成整批屏幕 DMA | 306 批 |
| 累计 QSPI 错误标志 | 0 |

随后通过调试器复位并重新运行，同一固件再次达到 520 MHz。复位后的 40 秒
快照确认：九组 PWM 均有采样，601,654 个有效快照零错误；三核校验零错误，
STM 中断 40,052 次且无周期超限，40 批屏幕 DMA 完成，QSPI 错误标志为零。
复位日志：`Debug/overclock-520-reset1.log`；快照：`Debug/overclock-520-reset1-final-result.json`。
板子最终保留此固件并继续运行。

这是本板在上述负载和当前供电下的约 5 分钟验证，不能据此保证其它代码、
温度、供电或长时间运行的稳定性。540 MHz 及以上的失败已经表明实际边界。

## 文件及复现

- 时钟与启动：`Configurations/Ifx_Cfg.h`、`Configurations/Ifx_Cfg_Ssw.h`。
- PWM/STM/屏幕测试：`Cpu0_Main.c`；其它核入口：`Cpu1_Main.c`、`Cpu2_Main.c`。
- 三核校验：`App/Hardware/overclock_test.h`。
- 构建镜像：`Debug/TC377_iLLD_Study.elf`、`Debug/TC377_iLLD_Study.hex`。
- 原固件备份：`Debug/overclock-original.hex`、`Debug/overclock-original.elf`。
- 调试读取：`Debug/overclock-read.ps1` 和 `Debug/overclock-mcd.exe`，读取脚本按当前 ELF 解析符号地址。
- 烧录与快照原始记录：`Debug/overclock-*-flash.log`、`Debug/overclock-*-read.log`、`Debug/overclock-*-result.json`。

`Debug/` 为已忽略的本机产物目录。烧录时保留 `-ucb off`，不改 UCB/BMHD。
本机 Flasher 完成后需要通过 MCD 继续运行 CPU；仅看到 Flash 校验通过不代表程序已运行。
当前固件会一直运行并显示统计，尚未把测试入口恢复为此前的屏幕性能演示。

## 参考

- [英飞凌 STM 培训：STM/SPB 整数时钟比例要求](https://www.infineon.com/assets/row/public/documents/10/56/infineon-aurix-system-timer-training-en.pdf?fileId=5546d46269bda8df0169ca92e404259c)
- [英飞凌 TC3xx 时钟及寄存器手册](https://www.infineon.com/assets/row/public/documents/10/44/infineon-aurix-tc3xx-part1-usermanual-en.pdf)
