$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$zig = Join-Path $projectRoot '.venv\Lib\site-packages\ziglang\zig.exe'
if (-not (Test-Path -LiteralPath $zig)) {
    $zig = (& python -X utf8 -c "import pathlib,ziglang; print(pathlib.Path(ziglang.__file__).parent / 'zig.exe')").Trim()
}
$buildDirectory = Join-Path $PSScriptRoot 'build'
$testExecutable = Join-Path $buildDirectory 'test_rc_capture_core.exe'
$chassisTestExecutable = Join-Path $buildDirectory 'test_chassis_math.exe'
$safetyTestExecutable = Join-Path $buildDirectory 'test_safety.exe'
$motorProfileTestExecutable = Join-Path $buildDirectory 'test_motor_profile.exe'
$motorDriverAdapterTestExecutable = Join-Path $buildDirectory 'test_motor_driver_adapter.exe'
$rcInputAdapterTestExecutable = Join-Path $buildDirectory 'test_rc_input_adapter.exe'
$rcMspTestExecutable = Join-Path $buildDirectory 'test_rc_msp.exe'
$diagnosticsTestExecutable = Join-Path $buildDirectory 'test_diagnostics.exe'
$startupSafetyTestExecutable = Join-Path $buildDirectory 'test_startup_safety.exe'
$lcdConfigTestExecutable = Join-Path $buildDirectory 'test_lcd_config.exe'
$lcdDisplayLayoutTestExecutable = Join-Path $buildDirectory 'test_lcd_display_layout.exe'
$motorDriverBenchTestExecutable = Join-Path $buildDirectory 'test_motor_driver_bench.exe'
$lcdDisplayBenchTestExecutable = Join-Path $buildDirectory 'test_lcd_display_bench.exe'
$fontTestScript = Join-Path $projectRoot 'tools\test_gen_chinese_font.py'

New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
& python $fontTestScript
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror `
    "-I$projectRoot\App" "-I$PSScriptRoot" `
    "$projectRoot\App\rc_capture_core.c" "$PSScriptRoot\test_rc_capture_core.c" `
    '-o' $testExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $testExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror `
    "-I$projectRoot\App" "-I$PSScriptRoot" `
    "$projectRoot\App\chassis_math.c" "$PSScriptRoot\test_chassis_math.c" `
    '-o' $chassisTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $chassisTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror `
    "-I$projectRoot\App" "-I$PSScriptRoot" `
    "$projectRoot\App\safety.c" "$PSScriptRoot\test_safety.c" `
    '-o' $safetyTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $safetyTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror `
    "-I$projectRoot\App" "-I$PSScriptRoot" `
    "$projectRoot\App\motor_profile.c" "$PSScriptRoot\test_motor_profile.c" `
    '-o' $motorProfileTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $motorProfileTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror `
    "-I$PSScriptRoot\fakes\motor_driver" "-I$projectRoot\App" "-I$PSScriptRoot" `
    "$projectRoot\App\motor_driver.c" "$PSScriptRoot\test_motor_driver_adapter.c" `
    '-o' $motorDriverAdapterTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $motorDriverAdapterTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror -DRC_BENCH_TEST=1 `
    "-I$PSScriptRoot\fakes\motor_driver" "-I$projectRoot\App" "-I$PSScriptRoot" `
    "$projectRoot\App\motor_driver.c" "$PSScriptRoot\test_motor_driver_bench.c" `
    '-o' $motorDriverBenchTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $motorDriverBenchTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror `
    "-I$PSScriptRoot\fakes\rc_input" "-I$projectRoot\App" "-I$PSScriptRoot" `
    "$projectRoot\App\rc_input.c" "$projectRoot\App\rc_capture_core.c" `
    "$PSScriptRoot\test_rc_input_adapter.c" '-o' $rcInputAdapterTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $rcInputAdapterTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror `
    "-I$PSScriptRoot\fakes\rc_msp" "-I$PSScriptRoot" `
    "$projectRoot\Src\stm32f4xx_hal_msp.c" "$PSScriptRoot\test_rc_msp.c" `
    '-o' $rcMspTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $rcMspTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror `
    "-I$PSScriptRoot\fakes\diagnostics" "-I$projectRoot\App" "-I$PSScriptRoot" `
    "$projectRoot\App\diagnostics.c" "$PSScriptRoot\test_diagnostics.c" `
    '-o' $diagnosticsTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $diagnosticsTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror `
    "-I$PSScriptRoot\fakes\startup_safety" "-I$projectRoot\App" "-I$PSScriptRoot" `
    "$projectRoot\App\startup_safety.c" "$PSScriptRoot\test_startup_safety.c" `
    '-o' $startupSafetyTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $startupSafetyTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror `
    "-I$projectRoot\App" "-I$PSScriptRoot" `
    "$PSScriptRoot\test_lcd_config.c" '-o' $lcdConfigTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $lcdConfigTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror `
    "-I$PSScriptRoot\fakes\diagnostics" "-I$projectRoot\App" "-I$PSScriptRoot" `
    "$projectRoot\App\lcd_display.c" "$PSScriptRoot\test_lcd_display_layout.c" `
    '-o' $lcdDisplayLayoutTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $lcdDisplayLayoutTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $zig cc -std=c11 -Wall -Wextra -Werror -DRC_BENCH_TEST=1 `
    "-I$PSScriptRoot\fakes\diagnostics" "-I$projectRoot\App" "-I$PSScriptRoot" `
    "$projectRoot\App\lcd_display.c" "$PSScriptRoot\test_lcd_display_layout.c" `
    '-o' $lcdDisplayBenchTestExecutable
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $lcdDisplayBenchTestExecutable
exit $LASTEXITCODE
