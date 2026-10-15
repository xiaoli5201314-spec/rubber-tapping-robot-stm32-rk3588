# STM32 MQTT 网页联调服务

本服务把 MQTT 设备消息转换为网页可读的状态 API。STM32 不再开 HTTP 服务器，也不发 HTTP /probe 请求；电脑端 HTTP 18080 保留用于提供页面和 API，设备通信使用 MQTT 1883。

## 启动与停止

1. 开启电脑 2.4 GHz 热点，让 STM32/ESP-12F 连接该热点。
2. 双击 启动网页联调.cmd。脚本默认提供网页 HTTP 18080 和 MQTT 1883，并在窗口打印本机各网卡地址。
3. 用浏览器打开窗口显示的地址，通常是 http://127.0.0.1:18080/。
4. 停止服务时双击 停止网页联调.cmd。脚本只处理本项目登记的进程，不停止外部 broker。

也可以从本目录执行：

~~~powershell
.\start.ps1 -NoBrowser
.\stop.ps1
~~~

服务需要 Python 3、paho-mqtt 2.x；若 1883 已有监听所有 IPv4 地址、可匿名连接/订阅的 Mosquitto，启动脚本会复用它，否则启动可用的 Mosquitto 程序。防火墙放行 Python 和 Mosquitto 的热点网络访问。不要直接打开本地 index.html 作为实物入口。

ESP-12F（ESP8266）负责 Wi-Fi/MQTT；接收器 CH1/CH2/CH5 PWM 连接 STM32，由 STM32 本地驾驶。后台仅发送 SESSION 和 LEDTEST，不发送运动、解锁或切割指令；Wi-Fi 断线不会替代 CH5 锁定或硬件断电。

## 设备协议

设备主题：f407/web_test/<24 位大写 UID>/{status,cmd,echo}。

- 固件标记为 mqttweb1、mqttrc1、mqttmotor30、mqttmotor60、mqttmotor100。status 为约每秒一次的非 retained 心跳，字段使用 mqtt=0/1，不含旧的 http 字段。
- 首次心跳 token 为 32 个 0；服务向 cmd 发布 SESSION:<32 位小写随机值>。设备回传该 token 前不会进入在线状态，未确认时服务重试同一挑战；网页/API 的握手字段为 mqttVerified。
- 板端重启导致 uptime 回退时，只有重新握手取得的新 token 能开启新心跳周期；初次 seq 相等或更高也可恢复，旧 token 和同周期重复序号仍被拒绝。
- server 字段是设备实际连接的 MQTT broker 地址，默认来自电脑热点网关；网页显示该字段用于诊断，浏览器使用启动窗口中的 HTTP 18080 地址。
- 在线要求 MQTT 会话已经确认、broker 连接正常且最新有效心跳年龄不超过 5 秒（age <= 5s）。服务端丢弃 retained、重复、倒序心跳；超过 5 秒无新心跳则离线并隐藏旧读数。
- echo 只接受当前 LEDTEST 请求对应的 LEDACK，自检 5 秒内没有本次匹配应答则超时。
- MQTT broker 断开会清除在线证据和进行中的 LED 自检；broker 恢复后必须等待新的 MQTT 心跳。

热点显示 ESP 已连接只证明 Wi-Fi 接入。固件还需确认 +MQTTCONNECTED、订阅命令主题并完成 SESSION 握手。AT+MQTTPUBRAW 采用异步状态机，等待提示符后发送精确长度数据，只有发送后的 +MQTTPUB:OK 确认发布；普通 OK 或 busy 不作发布成功证据。超时或断线后隔离迟到响应、重新同步串口再连接。

## API

- GET /api/status：返回当前实时状态，响应禁止缓存。
- POST /api/self-test：请求体只能是 {}，向当前在线 STM32 发布 LEDTEST 挑战。
- GET /health：当前服务的 application、pid、instanceId；broker 连接状态查看 /api/status 的 brokerConnected。

不存在底盘 /probe 接口；请不要填写底盘 IP、端口 80，也不要运行归档目录中的 HTTP 直连测试。

## 软件验证

从毕业设计根目录执行：

~~~powershell
python -m pytest -q upper_computer/web_link_host/tests
python upper_computer/web_link_host/tests/test_mqtt_integration.py
python upper_computer/web_link_host/tests/browser_check.py
~~~

测试使用隔离的 broker/API 和模拟 MQTT 客户端；通过不等同于实体 ESP-12F、热点或 STM32 已完成实测。
