# 第三方源码与生成依赖

本仓库的公开整理没有改变第三方代码许可，也没有为所有文件统一套用新许可证。

| 内容 | 来源 / 许可 | 发布处理 |
| --- | --- | --- |
| `Drivers/CMSIS/Include` | Arm CMSIS，文件头标 Apache-2.0 | 保留原文件头，附 `licenses/Apache-2.0.txt` |
| STM32F4 HAL、器件头、启动/系统模板及 Cube 生成部分 | STMicroelectronics，文件头标 BSD 3-Clause | 保留原文件头，附 `licenses/ST-BSD-3-Clause.md` |
| LCD 初始化表、ASCII 字模 | 用户另行取得的板卡 BSP | 只保留生成器与依赖说明，不提交生成数据 |
| 中文字体位图 | 本机字体经 Pillow 栅格化 | 不提交生成数据；本地生成须遵守字体许可 |
| Python、Paho MQTT、Pillow、Unicorn、Zig、Mosquitto | 各自上游发行版 | 以依赖方式获取，不随仓库复制安装包 |

ST HAL 上游：`https://github.com/STMicroelectronics/stm32f4xx-hal-driver`。
CMSIS 上游：`https://github.com/ARM-software/CMSIS_5`。

自有控制、联调、网页与概念建模工作本次未新增许可证。若需要将这些部分用于其他项目或再发布，请先与仓库所有者确认许可。
