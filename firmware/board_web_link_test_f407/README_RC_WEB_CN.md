# 遥控器网页测试：不接屏幕，不驱动电机

Keil 工程：`MDK-ARM/board_rc_web_test_f407.uvprojx`  
下载文件：`MDK-ARM/Objects_RC_Web/board_rc_web_test_f407.hex`  
网页心跳版本：`mqttrc1`。通信/遥控模拟目标分别生成 HEX，请按工程用途选择。

本目标不初始化 LCD/FSMC；即使没有屏幕也能完成测试。
接收器 PWM 是实测输入，左右输出是软件模拟指令，不是实际车速。
PB6/PB7、PD6/PD7、PE5/PE6 保持低电平，TIM4 不启动。
CH3/CH4 不参与此次测试，MQTT 与 PF10 LED 自检保留。
ESP-12F（ESP8266）负责 Wi-Fi/MQTT；遥控接收器用有线 PWM 接到 STM32，
不通过 ESP8266 或网页传递摇杆指令。STM32 在本地计算模拟输出。

## 接线与启动

| 接收器 | STM32 | 用途 |
| --- | --- | --- |
| CH1 信号 S | PB10 | 右摇杆左右 |
| CH2 信号 S | PB11 | 右摇杆上下 |
| CH5 信号 S | PF6 | SWA 锁定/解锁 |
| 电源 + | 稳定 5V | 接收器供电 |
| 电源 − | GND | 与 STM32 共地 |

1. 沿用当前 ESP-12F 接线、供电。电脑开启 2.4 GHz 热点 `YOUR_WIFI_SSID`，
   密码沿用 `Inc/test_config.h` 中的配置。
2. 如果旧网页服务正在运行，先双击
   `.\upper_computer\web_link_host\停止网页联调.cmd`，
   再双击同目录 `启动网页联调.cmd`，让后台加载新的遥控协议。
3. 用 ST-Link 下载上述 **RC 网页测试 HEX**，复位 STM32。
   使用启动窗口的 `http://127.0.0.1:18080/` 地址，按 Ctrl+F5 刷新，
   保持实时模式，进入“底盘监视”；不直接打开本地 index.html。
4. 开机后先保持右摇杆回中，将 SWA 拨至网页 CH5 约 1000 的位置。
   等待 MQTT SESSION 双向握手和新心跳，网页在线后设备状态页注明 `mqttrc1`。
   进入底盘监视后，CH1/CH2 约 1500 µs、三路周期约 20 ms，
   “遥控输入”有效，“锁定状态”显示等待解锁，左右指令为零。
   若 CH5 不随 SWA 改变，需在遥控器的通道分配中将 CH5 分配给 SWA。

## 测试顺序与正常现象

网页和 MQTT 心跳约每秒刷新。每个动作保持 1～2 秒再判断，
不要用网页判断毫秒级延迟。Wi-Fi 已连接不代表在线；
在线要求 SESSION 已确认、broker 正常且最新有效心跳年龄不超过 5 秒，
超过 5 秒则隐藏旧读数。

1. 保持 CH5 低，推动右摇杆：左/右使 CH1 约 1000/2000，
   下/上使 CH2 约 1000/2000，回中约 1500。左右模拟指令仍为零。
   若摇杆不回中，不能解锁。
2. 右摇杆回中，CH5 低至少 1 秒，再拨至 CH5 约 2000 的位置。
   “锁定状态”正常显示“就绪”，左右为零。
3. 测试以下动作，每项保持 1～2 秒，等模拟斜坡稳定：

| 右摇杆 | 模拟运动 | 左指令 | 右指令 |
| --- | --- | --- | --- |
| 向上拨满 | 前进 | +30% | +30% |
| 向下拨满 | 后退 | −30% | −30% |
| 向左拨满 | 左转 | −30% | +30% |
| 向右拨满 | 右转 | +30% | −30% |
| 回中 | 静止 | 0% | 0% |

4. 再将 SWA 拨回 CH5 低：锁定或回中后等待解锁，左右为零。
   程序内部立即停止模拟；网页在下一次上报时看到变化。
5. 关闭发射机，观察三路 PWM。
   接收器若停止输出，程序在超过 100 ms 无有效脉冲后判为无效，
   从就绪进入失效，网页随后显示无有效信号、旧脉宽留空、左右零。
   如果接收器持续输出最后的 PWM，单靠 STM32 脉宽检测无法确认无线失联，
   需检查接收器的失控保护设置；不能把仍显示“有效”当作保护通过。

整个测试实际电机都应保持停止。网页只读，不发运动/解锁指令。
PF10 仍用于设备状态页的 LED 自检；RK3588、摄像头保持未接入。

## 协议与软件核验

主题与原网页测试相同：`f407/web_test/<UID>/status|cmd|echo`。
`mqttrc1` 使用 MQTT 基础字段：

```text
fw=mqttrc1;mqtt=1;token=<32位小写hex>;ip=<板子IPv4>;server=<broker IPv4>;uptime=<秒>;seq=<uint32>;runled=0;cmdcount=<uint32>;rxerr=<uint32>;parseerr=<uint32>
```

首次 token 为 32 个 `0`，后台发送 `SESSION:<32位小写随机值>`；
STM32 在后续新心跳回传后才得到 `mqttVerified=true`，不再通过 HTTP /probe 取 token。
AT+MQTTPUBRAW 异步等待提示符和发布结果，普通 OK、busy 不作为发布成功证据；
超时或断线先隔离迟到回复、恢复串口同步，再重新连接。
基础字段后增加：

```text
ch1=1500;ch2=1500;ch5=1000;p1=20000;p2=20000;p5=20000;v1=1;v2=1;v5=1;state=WAIT;left=0;right=0;motor=0
```

`ch*`、`p*` 单位为 µs；`v*` 表示有效且新鲜。
无效或超时通道的脉宽、周期和有效标记同时为 0，
后台转换为网页空读数；STM32 在线与遥控输入有效分别判断。
状态为 LOCKED/WAIT/ARMED/FAILSAFE；
`left/right` 是有符号千分比，范围 ±300，只有 ARMED 可以非零；
`motor` 必须为 0。后台拒绝非法字段或状态组合。

在毕业设计根目录执行：

```powershell
$env:F407_PROTOCOL_TARGET = 'board_rc_web_test_f407'
python -X utf8 firmware/board_web_link_test_f407/Tools/test_rc_web.py
python -X utf8 firmware/board_web_link_test_f407/Tools/test_protocol.py
python -m pytest -q upper_computer/web_link_host/tests
# JavaScript syntax: node --check upper_computer/web_prototype/status.js
python upper_computer/web_link_host/tests/browser_check.py
Remove-Item Env:F407_PROTOCOL_TARGET
```

编译 HEX 的指令测试使用受控定时器、ESP 夹具；
网页截图 `preview/web_link/*rc_web_fixture.png` 使用受控 API。
这些仅是软件证据，实际接收器、电平、接线和失控保护需按上面步骤确认。
`Tools/make_rc_project.ps1` 可以从原网页工程重新派生相同的 RC 目标。
