# 发布副本验证

日期：2026-10-15。验证针对本次公开整理后的源码副本，使用本机工具和受控测试夹具；没有连接机器人硬件，没有烧录或发送物理运动指令。

| 检查 | 实际结果 |
| --- | --- |
| 网页状态、SESSION、遥控/电机遥测 Python 单元测试 | 45 passed，58 个 subtests passed |
| 隔离 Mosquitto + 实际 HTTP/API + 模拟设备 | 4 tests，OK |
| 实际 Keil ARM HEX 协议测试，60% 目标 | 18 tests，OK |
| 60% 电机目标的寄存器/指令夹具 | 20 tests，OK |
| MQTT 迟到/丢失响应及断线恢复 | 14 tests，OK |
| 实际 HEX → 受控 ESP 传输 → broker → 服务 → LEDACK | 1 test，OK |
| C 主机套件及中文生成器检查 | `run_host_tests.ps1` 返回 0，4 个字体测试及全部 C 断言通过 |
| Keil 带屏 web、无屏 RC、60% 实际电机三个目标 | 各 0 errors、0 warnings |
| 11 份 Keil 项目中的源码文件引用 | 所有列出的源码路径存在 |
| 网页 `app.js` / `status.js` | `node --check` 通过 |

编译使用 MDK 5.43.1、工程的 ARM Compiler 5 设置；Python 3.14、Unicorn、Mosquitto、Zig 0.16.0。C 套件通过时将 Zig 全局/局部缓存放在同一任务磁盘，避免当前机器跨设备缓存重命名失败。

带屏编译和 C/LCD 套件使用原工程已有的本地生成字库/初始化表。这些表因来源和许可边界不纳入 Git，外部使用者须按 [BUILD.md](BUILD.md) 本地生成。无屏目标不依赖这些 LCD 表。

60% ARM 电机测试同时设置 `F407_PROTOCOL_TARGET=board_chassis_web_f407_60pct` 与 `F407_MOTOR_VARIANT=60`。前者选择实际 HEX，后者选择手工核对的测试预期；两者不能混配。

本轮未重跑 30% / 100% 两个目标的完整指令套件、Windows 启停全套或浏览器截图检查。没有据本表宣称实物 Wi-Fi、遥控接收器、驱动器、电机负载、RK3588、视觉、雷达或割胶执行机构完成新一轮验收。

## 提交内容检查

三份真实 Wi-Fi 配置的 SSID / 密码原值在公开文本副本中均为 0 命中；使用占位配置、示例端点和虚拟设备 UID。暂存清单排除了 `.superpowers` 内部记录、私人文档、日志、缓存、对象文件、HEX/AXF、厂商 BSP 数据、CAD 输出和工具安装包。ST / Arm 许可与原文件头保留。
