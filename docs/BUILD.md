# 构建与本地生成依赖

## 工具

- Windows / Keil MDK 5，ARM Compiler 5.06 update 5 或兼容版本，STM32F4 器件支持包。
- Python 3.10+、`requirements-dev.txt`；MQTT 集成测试另需 Mosquitto。
- C 主机测试使用 `ziglang==0.16.0`，测试脚本也支持工程局部 `.venv`。

Keil 工程保留原目标和引脚设置；调试器选择、Flash 算法应在自己的机器核对。不要在编译/软件测试时连接电机功率电源。

## MQTT 无屏目标

在 `firmware/board_web_link_test_f407/Inc/test_config.h` 设置本地网络。以下目标不使用 LCD 生成头文件：

```powershell
UV4.exe -r .\firmware\board_web_link_test_f407\MDK-ARM\board_rc_web_test_f407.uvprojx -t board_rc_web_test_f407
UV4.exe -r .\firmware\board_web_link_test_f407\MDK-ARM\board_chassis_web_f407_60pct.uvprojx -t board_chassis_web_f407_60pct
```

HEX/map 由本地编译生成，不纳入版本库。`Tools/test_protocol.py`、`test_rc_web.py`、`test_motor_web.py` 使用 Unicorn 执行这些实际 ARM 指令，需先编译所选目标：

```powershell
$env:F407_PROTOCOL_TARGET = 'board_chassis_web_f407_60pct'
$env:F407_MOTOR_VARIANT = '60'
python -B firmware/board_web_link_test_f407/Tools/test_protocol.py
python -B firmware/board_web_link_test_f407/Tools/test_motor_web.py
python -B firmware/board_web_link_test_f407/Tools/test_mqtt_recovery.py
python -B firmware/board_web_link_test_f407/Tools/test_mqtt_bridge.py
Remove-Item Env:F407_PROTOCOL_TARGET
Remove-Item Env:F407_MOTOR_VARIANT
```

这些测试会产生本地结果文件；它们不证明物理板、Wi-Fi、接收器和电机验收通过。

## LCD 依赖

以下生成文件来自厂商 BSP 或用户机器所用字体，公开仓库不再分发这些数据：

- `lcd_nt35510_init.h`：NT35510 初始化寄存器表。
- `lcd_font_8x16.h`：ASCII 字模。
- `lcd_font_chinese.h`：从本机字体生成的中文点阵。

使用合法取得的板卡 BSP，将 `ALIENTEK_LCD_DIR` 指向同时包含 `lcd_ex.c` 和 `lcdfont.h` 的目录；生成器面向本项目原始 BSP 格式，其他版本须先核对：

```powershell
$env:ALIENTEK_LCD_DIR = 'C:\path\to\your\BSP\LCD'
python firmware/chassis_f407/tools/gen_lcd_data.py
python firmware/chassis_f407/tools/gen_chinese_font.py
Copy-Item firmware/chassis_f407/App/lcd_nt35510_init.h firmware/board_web_link_test_f407/Inc/
Copy-Item firmware/chassis_f407/App/lcd_font_8x16.h firmware/board_web_link_test_f407/Inc/
Copy-Item firmware/chassis_f407/App/lcd_nt35510_init.h firmware/board_wifi_lcd_test_f407/Inc/
Copy-Item firmware/chassis_f407/App/lcd_font_8x16.h firmware/board_wifi_lcd_test_f407/Inc/
python firmware/board_web_link_test_f407/Tools/generate_fonts.py
python firmware/board_wifi_lcd_test_f407/Tools/generate_fonts.py
```

中文生成器在 Windows 系统字体目录寻找字体，只在本地生成点阵；发布这些生成物前仍需自行确认字体授权。生成输出已列入 `.gitignore`。

## C 主机测试

```powershell
python -m pip install -r requirements-dev.txt
$env:PYTHONUTF8 = '1'
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PWD '.zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $PWD '.zig-cache/local'
.\firmware\chassis_f407\tests\run_host_tests.ps1
```

完整 C/LCD 主机套件会读取上述本地生成头文件，先完成 LCD 生成步骤。该脚本不访问实际串口或驱动电机。
