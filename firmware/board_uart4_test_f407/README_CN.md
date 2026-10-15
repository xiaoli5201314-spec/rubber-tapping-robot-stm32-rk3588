# 自制 STM32F407 板串口4 / CH340N 通信测试

工程：`MDK-ARM/board_uart4_test_f407.uvprojx`。
目标：`board_uart4_test_f407`。
固件：`MDK-ARM/Objects/board_uart4_test_f407.hex`。

测试链路为电脑 USB → 板上 Type-C → CH340N → STM32 UART4。
按现有原理图，PC10 是 MCU_TX4，接 CH340N 的 RXD；
PC11 是 MCU_RX4，接 CH340N 的 TXD。不需要额外接串口模块。

本工程独立于原 Wi-Fi、屏幕、遥控和底盘工程，使用 HSI 内部 16 MHz 时钟。
本次固件只初始化串口4及 PF9/PF10 指示灯，屏幕显示和联网功能不运行。
下载回之前的屏幕/Wi-Fi 固件可恢复原测试。

## 串口助手怎么设置

| 项目 | 设置 |
| --- | --- |
| 串口号 | USB-SERIAL CH340 对应端口，准备时为 COM9 |
| 波特率 | 115200 |
| 数据位 | 8 |
| 停止位 | 1 |
| 校验 | 无 / None |
| 流控 | 无 / None |
| 接收 | 文本显示，关闭 HEX 显示 |
| 发送 | 文本发送，关闭 HEX 发送 |
| 本地回显 | 关闭，便于确认文字确实由板子返回 |
| 自动/循环发送 | 先关闭，手动发送 |

给板子正常供电，通过 ST-Link 下载固件后，用能传数据的 USB 线连接板上
Type-C。打开串口助手对应端口，复位 STM32 可以看到完整启动提示；
即使打开助手时错过了启动提示，也会每秒收到持续增加的计数。
USB 插拔后 COM 编号可能改变，以设备管理器为准。

## 预期收到什么

启动提示：

```text
=== STM32F407 UART4 TEST ===
PC10 TX / PC11 RX / CH340N TYPE-C
115200 baud, 8N1, no flow control
Send PING, STATUS, HELP, or text to echo.
```

每秒发送类似这一行，具体计数由实际运行产生：

```text
UART4 tick=1 uptime_ms=1000 rx=0 tx=144 messages=0 uarterr=0 overflow=0 lineerr=0 txtimeout=0
```

看到这些行，只能确认 STM32 到电脑的发送通路。继续手动发送：

| 发送文本 | 板子回复 | 用途 |
| --- | --- | --- |
| `PING` | `PONG` | 确认双向通信 |
| `hello UART4` | `ECHO: hello UART4` | 确认发送内容被实际接收 |
| `STATUS` | `STATUS tick=... rx=...` | 查看当前计数 |
| `HELP` | 命令说明 | 查看支持的测试命令 |

命令使用大写。发送时勾选添加 CR/LF 换行，或者不勾选都可以。
收到 CR 或 LF 会立即处理；没有换行则收到数据后空闲约 80 ms 再回传。
一条消息最多 191 字节，超长或含 NUL 的消息会被丢弃并提示错误，
后续正常消息仍能处理。这是文本通信测试，不是任意二进制透传。
中文原样回传需让助手发送/接收编码保持一致；建议先用 ASCII 英文测试。

## 计数与 LED

- `tick`：累计发送的定时状态行数量。
- `uptime_ms`：STM32 复位后的运行时间。
- `rx`：收到且无 UART 硬件错误的字节数量，包括换行。
- `tx`：已经写入 UART 发送数据寄存器的字节数量；不会证明电脑已收到。
- `messages`：处理过的非空有效消息数量。
- `uarterr`：串口溢出、帧、噪声或奇偶校验错误次数。
- `overflow`：软件接收缓冲满，丢弃字节的次数。
- `lineerr`：超长消息或含 NUL 的消息数量。
- `txtimeout`：UART 发送等待超时次数。

RUNLED（PF10）持续闪烁表示主循环运行。
ERRLED（PF9）正常时灭，出现上述通信/输入错误后亮，复位会清除计数。
这两个灯都是低电平点亮。
Keil 调试器也可查看 `g_uart4_test_status`。

看到定时数据、手动发送 `PING` 收到 `PONG`、发送任意英文收到对应
`ECHO`，并且错误计数为 0，才完成本轮基本双向通信测试。
CH340 能被电脑识别不等于 STM32 的收发已经验证。

## 软件检查

`MDK-ARM/build.log` 保存 Keil 编译结果。
`Tools/test_compiled.py` 执行编译后的 ARM HEX，
检查 PC10/PC11 AF8、UART4 波特率和中断配置、接收缓冲、
CR/LF 和无换行回传、命令回复以及输入异常后的恢复。
结果在 `Tools/software_test.json`。
这些是软件检查，不代表实物串口已经测试通过。

## 实物测试记录

2026-10-11 22:31（北京时间），已通过 COM9 / 115200 对下载后的实际板子执行测试：
连续运行计数、PING/PONG、随机文字原样回传、不添加换行回传、
191 字节消息和六条不同长度的消息均通过。
本轮新增 11 条消息、529 个接收字节，实际计数与发送数量完全一致，
`uarterr / overflow / lineerr / txtimeout` 四项均为 0。
这是本轮基本双向通信验证，不包括长期运行或高速持续满负载测试。

报告：`Tools/physical_test.json`；逐条收发记录：`Tools/physical_traffic.json`。
测试完成已关闭电脑端 COM9，可以重新在串口助手打开。
需要再测试时，先关闭串口助手的串口连接，然后执行：

```powershell
python -X utf8 .\Tools\test_physical.py --port COM9
```
