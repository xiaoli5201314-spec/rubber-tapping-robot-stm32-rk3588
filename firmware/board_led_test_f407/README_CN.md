# 自制 STM32F407 板 LED 焊接测试

对应芯片：STM32F407ZGT6。

## 两颗 LED

依据自制板原理图 `tmp/sch4/leds.png`：

| 板上名称 | 器件 | 原理图颜色 | STM32 引脚 | 串联电阻 | 点亮方式 |
| --- | --- | --- | --- | --- | --- |
| ERRLED | LED3 | 红色 | PF9 | R15，1 kΩ | GPIO 输出低电平 |
| RUNLED | LED4 | 绿色 | PF10 | R16，1 kΩ | GPIO 输出低电平 |

两颗 LED 的阳极接 3.3 V，因此输出高电平熄灭，输出低电平点亮。
如果实际焊接的颜色不同，以 ERRLED/RUNLED 名称、位号和引脚为准。

## 下载后应看到什么

上电或按复位后，以下过程不断重复，每轮约 4.75 秒：

1. 两灯都灭，持续约 1 秒。
2. 只有 ERRLED（PF9）亮，持续约 1 秒。
3. 只有 RUNLED（PF10）亮，持续约 1 秒。
4. 两灯都灭约 0.25 秒，再一起闪烁 3 次，每次亮 0.25 秒、灭 0.25 秒。

程序使用内部 HSI 16 MHz 时钟和 SysTick 毫秒计时，不依赖外部晶振。
只初始化 PF9/PF10 与 SysTick，保留 SWD 引脚供下载使用。
此处是 LED 焊接测试，ERRLED 亮属于预设测试步骤，不表示检测到故障。

## Keil 工程与下载

- 工程：`MDK-ARM/board_led_test_f407.uvprojx`
- 测试代码：`Src/main.c`
- 时钟初始化：`Src/system_stm32f4xx.c`
- 编译后文件：`MDK-ARM/Objects/board_led_test_f407.hex`
- 编译日志：`MDK-ARM/build.log`

本工程自带所需 CMSIS 头文件和启动文件，不需要引用底盘控制工程。
编译器选择 ARM Compiler 5，目标为 STM32F407ZGTx；预设下载器为 ST-Link、SWD。

给板子正常供电，ST-Link 的 SWDIO 接 PA13、SWCLK 接 PA14、GND 共地。
如果下载器带 VTref/VAPP 电压参考输入，将它接板子的 3.3 V。
不要把 ST-Link 的 5 V 引脚接到板子的 3.3 V 电源网。
BOOT0 应为低电平，保证复位后从 Flash 启动。

用 Keil 打开工程，点击 Rebuild（编译），再点击 Download（下载）。
下载完成后按板子复位键或重新上电，观察上述灯光顺序。
此次仅准备并编译工程，没有自动下载到硬件。

## 能判断哪些问题

正确循环说明主控能执行 Flash 中的程序、基本运行电源和复位条件可用，
且 PF9/PF10 的 LED 路径能够亮灭。这不代表其他接口或整块 PCB 已全部验证。
外部晶振、LCD、遥控器接口、电机驱动接口和 Wi-Fi 模块需要分别测试。

若只有一颗灯正常，优先检查不亮那一路的 LED 极性、电阻及焊点。
若两颗都不亮，先确认下载是否成功、BOOT0 是否为低、是否复位运行，
再检查 3.3 V、主控焊接以及 LED 公共电源连接。
本测试不初始化屏幕，因此屏幕不会显示测试界面。

## 依赖来源

`Drivers/CMSIS` 与 `MDK-ARM/startup_stm32f407xx.s` 从项目现有的
`firmware/chassis_f407` 复制，保留了原文件的版权和许可声明。
