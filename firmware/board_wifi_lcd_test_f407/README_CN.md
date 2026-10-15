# 自制 STM32F407 板：中文竖屏与 Wi-Fi 联合测试

Keil 工程：`MDK-ARM/board_wifi_lcd_test_f407.uvprojx`。
可下载 HEX：`MDK-ARM/Objects/board_wifi_lcd_test_f407.hex`。
使用 STM32F407ZGT6、HSI 16 MHz、ESP-12F AT+MQTT 固件。

沿用之前的正点原子 4.3 寸 NT35510 屏幕，480×800 竖屏，
浅色背景、深绿色标题，中文 16/24/48 点阵。字体存在 STM32 内部 Flash，
本界面不需要外部字库芯片或帧缓存。
原底盘工程、遥控工程和已通过的独立 Wi-Fi 工程均保留。
本测试初始化 LCD、Wi-Fi 串口和 LED，不驱动电机或切割装置。
触摸接口不初始化。

## 怎么测试

1. 板子断电，把原来的屏幕插到自制板的 2×17 LCD 插座。
   对准原理图与实物的 1 脚，不要把插座翻转或错开一排。
2. 打开本文件同目录下 `MDK-ARM` 中的工程，选目标
   `board_wifi_lcd_test_f407`，通过 ST-Link 下载后复位。
3. 正常情况下，屏幕在启动几秒内先出现“联网测试”界面。
   即使手机热点未开、电脑服务未启动，也应该有屏幕界面和运行时间。
   底部红、绿、蓝、白四色条可检查基本颜色。
4. 开启手机热点 `YOUR_WIFI_SSID`。密码沿用原来的配置，不显示在屏幕上。
   电脑连接同一热点，测试电脑 IP 仍配置为 `192.0.2.10`。
5. 电脑 HTTP/MQTT 测试服务沿用已有 Wi-Fi 测试工具：
   `.\firmware\board_wifi_test_f407\Host\start_test.ps1`。
   本版本兼容同一个服务、端口、主题和验证协议。
6. 联网后，屏幕更新 Wi-Fi 状态和模块实际 IP；随后显示 HTTP 测试、
   MQTT 连接、双向回传次数与电脑验证结果。
   只有电脑完成 HTTP 随机 token 和 MQTT 双向挑战验证，且当前连接正常、
   本轮没有串口/解析错误时，顶部才显示绿色“通过”。

电脑 IP 改变时修改 `Inc/test_config.h` 中的 `TEST_SERVER_HOST`，
重新编译、下载；沿用旧启动脚本时同步其原工程配置中的服务器 IP。
测试服务默认运行两小时，过期后可重新运行启动脚本。
原 `Host/result.json` 可能保留上一次通过记录，检查本次时间、
HTTP 请求和 MQTT 收发是否已更新，不能用旧 PASS 判断新版本已实测。

## 界面含义

| 区域 | 显示内容 |
| --- | --- |
| 测试状态 | 启动、检测、连接、测试、等待、异常、重试、通过 |
| 网络连接 | 热点名称、连接状态、模块当前 IP |
| HTTP | 未开始、测试中、已通过、待重试 |
| MQTT | 未开始、连接中、已连接、待重试 |
| 电脑验证 | 待验证/已通过、成功双向回传次数、AT 串口波特率 |
| 运行信息 | 上电运行时间、本轮串口接收和解析错误计数 |

回传次数累计到 STM32 复位；连接断开后它可以保留历史计数，
但“已通过”和 IP 会被撤销。HTTP/MQTT 失败会自动重试。
电脑测试服务未启动时出现“待重试”属于联网测试结果，
屏幕显示功能仍可以独立观察。

屏幕按改变的字段刷新，每次最多刷新一个区域，先处理 Wi-Fi 接收数据；
不循环整屏清屏。数值区域先完整擦除，再绘制新文字，避免旧文字残留。
实际刷新时间可在 Keil 中查看 `g_lcd_max_field_ms`。
MQTT 状态附加 `fw=lcd1` 和 `drawms=...`，用于区分这一版并观察实际刷新耗时；
旧电脑验证服务接受这些附加字段。

## LCD 接线依据

LCD 控制器初始化表、ASCII 点阵和中文点阵生成方式沿用原已使用的屏幕工程。
GPIO/延时已适配当前不使用 HAL 的独立测试工程。

| LCD 信号 | STM32 引脚 |
| --- | --- |
| CS | PG12 / FSMC_NE4 |
| RS | PF12 / FSMC_A6 |
| RD | PD4 / FSMC_NOE |
| WR | PD5 / FSMC_NWE |
| D0 / D1 | PD14 / PD15 |
| D2 / D3 | PD0 / PD1 |
| D4～D12 | PE7～PE15 |
| D13 / D14 / D15 | PD8 / PD9 / PD10 |
| BL 背光 | PB15，高电平开启 |
| RESET | 按自制板原理图连接 NRST |

16 位 FSMC Bank 4，基地址 `0x6C000000`；
命令地址 `0x6C00007E`，数据地址 `0x6C000080`。
Wi-Fi 仍用 PA2/PA3 的 USART2，LED 仍为 PF9/PF10，日志仍从 USART1 输出。

## 已执行的软件验证

- Keil 完整编译，日志在 `MDK-ARM/build.log`。
- 用实际编译后的 ARM HEX 验证 AT/IPD/HTTP/MQTT 解析与验证协议；
  报告在 `Tools/protocol_test.json`。
- 用实际 ARM 指令检查 FSMC/GPIO 初始化、像素写入及边缘裁切，
  验证 LCD 初始化保持 LED/UART 引脚。
- 执行编译后的界面代码，使用固件本身的字模生成三张软件预览，
  检查中文、最长 IP、最大计数、异常状态和各数值区域边界；
  报告在 `Tools/lcd_test.json`。

`Tools/preview_boot.png`、`preview_connected.png`、`preview_server_wait.png`
是软件预览，联网状态使用示例数据，不是实物屏幕照片。
这一版尚未下载到自制板，软件验证不能证明 LCD 硬件已通过。

再次运行验证工具使用已安装 Pillow、Unicorn 的 Python 环境：

```powershell
python -X utf8 .\Tools\test_protocol.py
python -X utf8 .\Tools\verify_lcd.py
```

`Tools/generate_fonts.py` 复用原底盘工程的字体生成器，
仅需修改显示文字时重新生成，之后重新编译固件。
