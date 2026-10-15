# STM32F407 MQTT 网页通信与 LED 自检固件

2026-10-14 的设备链路统一使用 MQTT，STM32 的 HTTP 直连和 /probe 已移除。
电脑 HTTP 18080 继续提供网页和 API，MQTT 1883 提供设备消息通信。
历史 HTTP 直连工程和产物移入 `_retired_http_direct_20261014`，不作为当前下载入口。

| 用途 | 固件标记 | Keil 工程 | HEX |
| --- | --- | --- | --- |
| 带屏通信、LED 自检 | `mqttweb1` | `MDK-ARM/board_web_link_test_f407.uvprojx` | `MDK-ARM/Objects/board_web_link_test_f407.hex` |
| 无屏遥控输入、模拟输出 | `mqttrc1` | `MDK-ARM/board_rc_web_test_f407.uvprojx` | `MDK-ARM/Objects_RC_Web/board_rc_web_test_f407.hex` |
| 无屏实际电机，30% | `mqttmotor30` | `MDK-ARM/board_chassis_web_f407_30pct.uvprojx` | `MDK-ARM/Objects_Motor_Web_30pct/board_chassis_web_f407_30pct.hex` |
| 无屏实际电机，60% | `mqttmotor60` | `MDK-ARM/board_chassis_web_f407_60pct.uvprojx` | `MDK-ARM/Objects_Motor_Web_60pct/board_chassis_web_f407_60pct.hex` |
| 无屏实际电机，100% | `mqttmotor100` | `MDK-ARM/board_chassis_web_f407.uvprojx` | `MDK-ARM/Objects_Motor_Web/board_chassis_web_f407.hex` |

遥控模拟步骤见 `README_RC_WEB_CN.md`，实际电机步骤见 `README_MOTOR_WEB_CN.md`。
现有 24V、5.1A 电源的后续常规试验优先选 60%，保留 30%/100% 对比；
PWM 上限不是电流限制，也不能据此认定供电已通过带载验证。

## 接线与启动

ESP-12F（ESP8266）需要支持 `AT+MQTTPUBRAW` 的 AT+MQTT 固件。
PA2/PA3 是 USART2 TX/RX，接 ESP RXD0/TXD0；
USART1 PA9/PA10 为独立调试口，115200 baud、8N1。
PF9 错误灯、PF10 运行灯均低电平亮。带屏目标保留 NT35510 480×800 中文竖屏。

ESP8266 只负责 Wi-Fi/MQTT，接收器 CH1/CH2/CH5 PWM 接到 STM32，
实际电机目标在本地处理遥控和安全状态。网页只读监视，不发运动或解锁指令。
断 Wi-Fi 会丢失网页状态，但不会替代 CH5 锁定或切断电机功率电源。

1. 开电脑 2.4 GHz 移动热点 `YOUR_WIFI_SSID`，密码使用 `Inc/test_config.h` 的当前配置。
2. 双击 `.\upper_computer\web_link_host\启动网页联调.cmd`。
   启动窗口提供网页地址，通常为 `http://127.0.0.1:18080/`。
   不直接打开本地 index.html，也不填写底盘 IP 或端口 80。
3. 给板子断电后核对 ESP 供电和串口。只有带屏 `mqttweb1` 目标需要接 LCD。
   该目标和 `mqttrc1` 将 PB6/PB7、PD6/PD7、PE5/PE6 保持低电平，不启动电机 PWM；
   `mqttmotor*` 会实际输出电机控制，下载前断开驱动器功率电源并按电机说明操作。
4. 打开上表对应工程，Rebuild 后用 ST-Link/SWD 下载该目标并复位。
   Debug 选择 ST-Link、SW；Flash Download 使用 `STM32F4xx 1MB Flash`，
   起始地址 `0x08000000`。编译和启动网页不会自动向板子烧录。
5. 观察调试串口的 Wi-Fi IP、`+MQTTCONNECTED`、SESSION 和状态发布。
   电脑热点显示 ESP 已连接只证明 Wi-Fi 接入，不能证明 MQTT 或 STM32 在线。
   带屏目标显示 `HOST`、`MQTT` 和 `SESSION`；`HOST` 是实际 broker 地址。
6. 网页实时模式应显示对应固件标记、MQTT 已验证，心跳序号继续更新。
   在握手完成且最新心跳年龄不超过 5 秒时，才能点击 LED 自检。
   PF10 应亮约 80 ms、灭约 80 ms、再亮约 80 ms，最终灭；
   完成亮灭后才发布本次 `LEDACK`，网页收到匹配应答后才显示成功。
7. 断板子电源或热点，网页超过 5 秒无新有效心跳后应离线并隐藏旧读数。
   恢复后固件重新连接并报告新心跳；重启电脑服务后通过 MQTT 重新取得 SESSION，
   无需等待旧的 HTTP 探测周期。

默认 `TEST_SERVER_HOST` 为空，固件使用 `AT+CIPSTA?` 返回的热点网关。
电脑不直接提供热点时，网关可能是手机或路由器，应将该配置改为电脑实际 IPv4，
重新编译和下载。防火墙允许 Python 与 Mosquitto 访问热点网络。

## 协议和在线证据

设备 ID 是 STM32 UID 的 24 位大写十六进制，主题为
`f407/web_test/<UID>/status`、`.../cmd`、`.../echo`。
普通状态约每秒使用非 retained MQTT QoS0 原始发布：

```text
fw=mqttweb1;mqtt=1;token=<32小写hex>;ip=<板子IPv4>;server=<broker IPv4>;uptime=<秒>;seq=<uint32>;runled=0;cmdcount=<uint32>;rxerr=<uint32>;parseerr=<uint32>
```

首次 token 为 32 个 `0`。电脑发布 `SESSION:<32位小写随机值>`，
STM32 在后续新心跳回传该 token 后，后台标记 `mqttVerified=true`。
仅当会话确认、broker 连接正常且最新有效心跳年龄 `age <= 5s` 时在线。
后台拒绝 retained、重复、倒序或非法心跳，broker 断开立即撤销在线证据。
RC 和电机目标还追加 PWM、安全状态和输出字段，见各自说明。

AT+MQTTCONN 的普通 `OK` 不等于连接完成，固件还等待 `+MQTTCONNECTED` 和订阅成功。
AT+MQTTPUBRAW 采用异步状态机：等待 `>` 后发送精确长度数据，
只有数据发送后的 `+MQTTPUB:OK` 确认发布；普通 `OK`、提前到达的发布结果和
`busy` 都不能确认成功。超时或断线后先隔离迟到回复、恢复串口事务边界，
再重新连接；提示符丢失时先填满已声明的 RAW 长度，避免新 AT 命令被当作数据。
这些等待不作为遥控电机控制的时基。

LED 命令为 `LEDTEST:<32位小写随机值>`，应答为 `LEDACK:<相同值>:OK`。
每段闪灯从实际 GPIO 写入开始计时，请求超过 5 秒作废。
MQTT 使用 clean session 和 QoS0 命令订阅；SESSION 可在首次心跳前接收，
LEDTEST 执行前需当前会话的状态发布已完成。若电脑已收到首条匹配心跳，
但 ESP 的发布结果尚未返回，仅暂存一条有效 LEDTEST，待该心跳明确成功后执行；
自检期限仍从收到命令起算 5 秒，发布失败、会话改变、断线或过期均取消等待。
更早到达或属于旧会话的命令不执行。断线会撤销灯测试和旧 token。
已接受的 LED nonce 在 MCU 本次启动期间去重，ACK 发布失败可重试相同应答，
不会重新闪灯；有限 Bloom 去重表也可能拒绝新的 nonce，此时重新点击自检。
ESP AT 回调不提供消息年龄或 retained 标记，后台只发布在线的新请求，
非 retained、无离线队列。

`cmdcount` 统计完成的本地闪灯，回传计数统计成功发布的 ACK；
屏幕“已通过”不代表电脑已收到 ACK，以网页的本次匹配结果为准。
摄像头、RK3588 和切割装置未接入本测试。

## 编译和软件验证

从 `.` 执行，下面为带屏目标示例：

```powershell
UV4.exe -r '.\firmware\board_web_link_test_f407\MDK-ARM\board_web_link_test_f407.uvprojx' -t 'board_web_link_test_f407' -o '.\firmware\board_web_link_test_f407\MDK-ARM\build.log'
$env:F407_PROTOCOL_TARGET = 'board_web_link_test_f407'
python -X utf8 .\firmware\board_web_link_test_f407\Tools\test_protocol.py
python -X utf8 .\firmware\board_web_link_test_f407\Tools\verify_lcd.py
Remove-Item Env:F407_PROTOCOL_TARGET
python -X utf8 .\firmware\board_web_link_test_f407\Tools\test_mqtt_recovery.py
python -X utf8 .\firmware\board_web_link_test_f407\Tools\test_mqtt_bridge.py
```

协议测试读取实际编译的 ARM HEX 和 map，检查 MQTT 会话、原始发布长度/应答、
LED GPIO、去重、断线及超时。恢复测试检查延迟/丢失 AT 回复，
桥接测试将受控 ESP 传输连接到隔离 broker 和电脑服务。
LCD 测试检查 FSMC、字模和状态字段；`Tools/preview_*.png` 是软件预览。
这些软件证据不等同于实体 ESP、热点、LED、接收器、驱动器和电机实测通过。
