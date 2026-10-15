# 自制 STM32F407 板 ESP-12F 联网测试

该工程独立于底盘、LED 和遥控器工程。芯片为 STM32F407ZGT6，使用内部
HSI 16 MHz；Wi-Fi 使用你板上已焊接的 ESP-12F / ESP8266 AT+MQTT 模块。
本次不改写 ESP8266 固件，也不初始化电机、LCD 或切割机构。

## 下载及联网

1. 手机开启热点 `YOUR_WIFI_SSID`，密码已经写入 `Inc/test_config.h`。
   使用 2.4 GHz、WPA2；准备时电脑已连接这个热点。
2. 电脑留在同一热点，运行 `Host/start_test.ps1` 启动测试服务。
   当前固件配置电脑 IP 为 `192.0.2.10`。
   如果电脑 IP 改变，修改 `TEST_SERVER_HOST` 后重新编译、下载。
3. Keil 打开 `MDK-ARM/board_wifi_test_f407.uvprojx`，
   编译并通过 ST-Link 下载，然后复位主控。
4. 程序自动探测模块串口波特率、检查 AT 响应、连接热点并获取 IP，
   然后测试 HTTP 和 MQTT。电脑端自动记录实际收发结果。
5. 下载后等待约 1 分钟。查看 `Host/result.json`：
   `state: PASS` 且 `physical_board_verified: true` 表示板子实际测试通过。
   `WAITING_FOR_BOARD` 表示尚未看到板子网络流量，不能判断硬件已通过。

电脑服务默认运行两小时。关机、关闭服务或热点改变后，重新运行启动脚本。
可用 `Host/stop_test.ps1` 只停止本测试的服务和 broker。
正常启动优先连接已运行的 Mosquitto 服务；没有服务时启动本项目的临时 broker。
不改动其安装目录中的配置，停止脚本也不会关闭已有的 Mosquitto 系统服务。
测试 MQTT 为同一热点内的 TCP 1883；HTTP 为 TCP 18080。
此次测试覆盖普通 HTTP 和 MQTT，不覆盖 HTTPS、MQTTS 或云平台鉴权。

## 怎样判断确实通过

HTTP：板子向电脑发送 GET 请求。电脑每次生成新的随机 token；
板子必须实际收到 HTTP 200 和这个 token，并通过 MQTT 上报相同 token。

MQTT：模块连接电脑的 broker、订阅专属命令主题，并发布测试状态。
电脑发出新的随机挑战，板子收到后回传。电脑确认内容相同，
且 HTTP token 正确、没有本轮串口丢数据，才记录实际板子的 PASS。
电脑再回发 VERIFIED 确认，使板上的 RUNLED 常亮。

电脑 IP 能访问热点不代表热点允许两台客户端彼此访问。
若热点隔离客户端，HTTP/MQTT 会失败；可以改用允许设备互通的 2.4 GHz 路由器。
手机热点开启后不需要手机上另外安装 HTTP 或 MQTT 软件。

## LED 和调试日志

| 现象 | 含义 |
| --- | --- |
| RUNLED（PF10）闪烁 | 正在测试或等待电脑验证 |
| ERRLED（PF9）亮、RUNLED 闪烁 | AT、联网、HTTP、MQTT 或串口解析出现问题 |
| ERRLED 灭、RUNLED 常亮 | 电脑已确认本轮 HTTP 与 MQTT 收发通过 |

两颗 LED 都是低电平点亮。等待服务未启动时，错误灯可能亮；
程序会重试 HTTP 和 MQTT，无需每次重新烧录。
在 Keil 调试器也可查看全局变量 `g_wifi_test_status`，包括各阶段结果、
Wi-Fi 波特率、串口错误、HTTP/MQTT 状态和电脑确认状态。

串口日志从已引出的 USART1 输出，115200、8N1：

| USB 转串口模块 | 你的 STM32 板 |
| --- | --- |
| RXD（3.3 V 逻辑） | PA9 / MCU_TX1 |
| TXD（3.3 V 逻辑，可选） | PA10 / MCU_RX1 |
| GND | GND |

板子独立正常供电。该测试不需要故障的板上 Type-C/CH340N 接口。
USART1 目前用于输出日志；后续接 RK3588 时再编写通信协议。
本程序不会在日志中输出热点密码。

板内 Wi-Fi 连接保持原 PCB 接线：
STM32 PA2 / USART2_TX 接 ESP RXD0（本模块原理图和厂家规格书的 15 脚），
STM32 PA3 / USART2_RX 接 ESP TXD0（本模块原理图和厂家规格书的 16 脚）。
模块资料是 ESP-12F，不是 ESP32 模块。

## 文件位置及验证范围

- `MDK-ARM/Objects/board_wifi_test_f407.hex`：可下载固件。
- `MDK-ARM/build.log`：Keil 实际编译日志。
- `Host/server.log`：实际 HTTP/MQTT 测试服务日志。
- `Host/result.json`：实际板子的网络测试结果。
- `Host/self_test.json`：电脑端 HTTP/broker/挑战回传及失败判定测试。
- `Host/protocol_test.json`：用实际编译的 ARM 指令测试 AT/IPD/MQTT 解析。

软件测试结果不等于板子已联网成功。
尚未下载固件、尚未收到实际板子网络流量时，不会记录硬件 PASS。
如果 Wi-Fi 模块完全没有 AT 响应，HTTP/MQTT 服务无法替代串口诊断；
这时用上面的 USART1 或 Keil 调试器查看失败阶段。
固件会尝试 115200、9600、57600、38400、19200、74880 波特率，
不会永久改变模块串口设置。

## 指令资料

以用户 `WiFi模块资料` 中提供的 Espressif ESP-AT v2.2.0.0 ESP8266 文档为准。
MQTT 用 AT+MQTTUSERCFG、AT+MQTTCONNCFG、AT+MQTTCONN、
AT+MQTTSUB、AT+MQTTPUB 和 +MQTTSUBRECV。
HTTP 用 TCP 连接发送标准 HTTP GET，兼容没有 HTTPCLIENT 指令的 AT 固件。
