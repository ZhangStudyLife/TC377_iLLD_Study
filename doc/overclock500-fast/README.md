# 500 MHz + 高速外设验证证据

本目录对应 CPU500、GTM/CMU/Cluster0/1 333.333、STM/SPB/MCANH/GETH166.667 MHz。
频率为寄存器与20 MHz晶振声明值计算，未使用外部频率计。

- `firmware-sha256.json`：被测最终 ELF/HEX 哈希。
- `flash.txt`、`start.json`：最终镜像烧录校验和初始运行快照。
- `watchdog-1..4.json/.txt`：CPU0/1/2及Safety停喂后，无调试器run介入的自主复位恢复及对应诊断位。
- `coldboot-final.json`：用户最终一次断电重上电，全部最终时钟生效；随后重新进行连续观察。
- `coldboot.json`：用户断电重上电后快照；CPU/GTM/STM/Cluster配置与最终镜像相同，但GETH仍125 MHz。之后仅将GETHDIV从4改为3；该快照不作为最终GETH166.667 MHz镜像断电验证的证据。

所有启动遗留状态保留在bootAlarms/sshBoot；运行期状态另见alarmSeen/sshSeen，不将清启动标志等同于消除硬件故障。
CAN使用片内虚拟总线，GETH未测试收发，仅核对时钟配置；ADC按用户要求未测。

断电前的15次有效快照覆盖uptime23–662秒，COM10四轮共4 MiB零错误。用户再次断电导致旧监测脚本按预期检测到uptime回退；该次中断不记作固件自发复位，也不将断电前后时间相加声称连续运行。

最终冷启动后的28次持续快照为`soak-0..27.json`，汇总为`summary.json`，最后一次为`final500.json`。
`before-powercycle-0..14.json`对应用户再次断电前的有效记录。
`uart-1..4.json`为断电前4 MiB；`uart-coldboot.json`为最终冷启动后1 MiB。

`baseline300.json`、`uart-baseline300.json`为同代码300 MHz回归；`restored500.json`及`restored-sha256.json`证实最后恢复被测500 MHz镜像。
