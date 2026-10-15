# STM32F407 + RK3588 移动式割胶机器人开发

履带底盘遥控、STM32 本地安全控制和 MQTT 网页监视的阶段性源码。当前可运行的软件主线是 STM32F407 接收 CH1/CH2/CH5 遥控 PWM，在本地完成锁定、回中解锁、失效停车和双电机输出；ESP-12F 负责 Wi-Fi/MQTT，电脑服务提供网页、在线证据和 LED 自检。

截至 2026-10-15，自制 STM32 板的 LED、USB 串口、Wi-Fi/MQTT、遥控 PWM 和底盘离地试验已推进；30%/60%/100% PWM 对比已开展，并定位到现有 24 V / 5.1 A 供电的限制。RK3588 的 Ubuntu 24.04 / ROS 2 Jazzy 软件环境已完成软件侧验证。逐项证据边界和下一步见 [当前进度](docs/progress.md)。

视觉辅助、雷达导航和割胶执行机构仍是后续实物集成部分，本仓库没有将它们表述为已经完成的自主割胶系统。独立的 MID-360S 历史开发见账号下的 `mid360-lidar-development`，它的 ROS 2 Humble 环境不等于本机器人已完成雷达集成。

## 工程关系

| 目录 / Keil 目标 | 用途 | 输出边界 |
| --- | --- | --- |
| `firmware/chassis_f407` | 基础履带遥控、安全状态机、LCD/遥控台架 | 基础底盘工程；MQTT 工程复用其核心模块 |
| `board_web_link_test_f407` / `mqttweb1` | 带屏通信、SESSION 握手、LED 自检 | 电机控制脚保持停止 |
| `board_rc_web_test_f407` / `mqttrc1` | 无屏遥控输入、网页遥测 | 左右输出为软件模拟指令 |
| `board_chassis_web_f407_30pct` / `mqttmotor30` | 实际电机输出目标 | PWM 上限 30% |
| `board_chassis_web_f407_60pct` / `mqttmotor60` | 实际电机输出目标 | PWM 上限 60% |
| `board_chassis_web_f407` / `mqttmotor100` | 实际电机输出目标 | PWM 上限 100% |
| `board_led_test_f407`、`board_uart4_test_f407` | 独立焊接、串口检查 | 分阶段排查入口 |
| `board_wifi_test_f407`、`board_wifi_lcd_test_f407` | 早期 Wi-Fi / LCD 联调 | 保留源码作参考，日常入口采用当前 MQTT 工程 |

五个 MQTT Keil 目标均在 `firmware/board_web_link_test_f407/MDK-ARM`。PWM 百分比不是实际车速，也不是电流限制。当前没有编码器测速闭环；网页不发送行驶、解锁或切割指令。

## 目录

- `firmware`：C 源码、Keil 项目、必要 CMSIS/HAL 源码和主机测试。
- `upper_computer/web_link_host`：Python MQTT 到 HTTP/API 服务、Windows 启停脚本及测试。
- `upper_computer/web_prototype`：实际服务所用的 HTML/CSS/JavaScript 页面。
- `mechanical/lift_concept`：Fusion 四柱升降概念生成脚本；不含个人编辑后的 F3D/STEP 成果。
- `docs`：构建、依赖和本次发布验证说明。

## 网页服务

安装 Python 3.10+，并将 Mosquitto 加入 PATH。Python 依赖：

```powershell
python -m pip install -r requirements-dev.txt
.\upper_computer\web_link_host\start.ps1 -NoBrowser
```

浏览器打开 `http://127.0.0.1:18080/`。设备 MQTT 默认端口为 1883。停止本项目服务：

```powershell
.\upper_computer\web_link_host\stop.ps1
```

首次运行前，在相应固件的 `Inc/test_config.h` 填写自己的 `WIFI_SSID`、`WIFI_PASSWORD`；仓库仅保留占位符。当前 MQTT 目标默认从热点网关取得 broker 地址，使用其他网络时须配置电脑实际地址。网页在线必须满足 SESSION 回传、broker 正常及 5 秒内有效新心跳，热点“设备已连接”本身不构成在线验证。

## 构建与验证

Keil 项目使用 STM32F407ZG、ARM Compiler 5 设置。无屏 RC/电机目标可直接从源码重建；LCD 目标的厂商初始化表和本地字体生成步骤见 [构建说明](docs/BUILD.md)。编译不自动烧录。

```powershell
python -B -m pytest -p no:cacheprovider -q upper_computer/web_link_host/tests/test_host.py upper_computer/web_link_host/tests/test_mqtt_session.py upper_computer/web_link_host/tests/test_rc_telemetry.py
python -B upper_computer/web_link_host/tests/test_mqtt_integration.py
```

验证记录及未覆盖范围见 [发布验证](docs/VALIDATION.md)。实车首次通电须使履带离地，并准备保险、硬件急停和独立断电。任何软件测试通过均不替代接收器失控保护、实际接线、电源和驱动器带载验收。

## 发布范围与许可

不含论文、学生资料、厂商手册/完整 SDK、系统与固件镜像、工具安装包、真实热点密码、设备运行日志和构建产物。Wi-Fi、私有网络地址、部署设备标识和本机路径已模板化。

本次未为自有工作擅自选择开源许可证；公开可读不等于授予任意再分发许可。ST、Arm 以及其他第三方部分保留原版权和许可，详见 [第三方与生成依赖](THIRD_PARTY_NOTICES.md)。
