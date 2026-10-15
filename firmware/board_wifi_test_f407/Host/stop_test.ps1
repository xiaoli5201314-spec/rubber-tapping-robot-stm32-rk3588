$ErrorActionPreference = 'Stop'
$wifiPidFile = Join-Path $PSScriptRoot 'server.pid'
if (-not (Test-Path -LiteralPath $wifiPidFile)) { exit 0 }
$wifiSavedPid = [int](Get-Content -LiteralPath $wifiPidFile)
$wifiProcess = Get-CimInstance Win32_Process -Filter "ProcessId=$wifiSavedPid"
if (-not $wifiProcess) { Write-Output 'Test server already stopped.'; exit 0 }
if ($wifiProcess.CommandLine -notlike '*board_wifi_test_f407*test_server.py*') {
    throw 'PID belongs to a different program; refusing to stop it.'
}
# Windows lacks a general SIGTERM handler. Stop only this test's broker child.
$wifiChildren = Get-CimInstance Win32_Process -Filter "ParentProcessId=$wifiSavedPid"
foreach ($wifiChild in $wifiChildren) {
    if ($wifiChild.Name -eq 'mosquitto.exe' -and
        $wifiChild.CommandLine -like '*board_wifi_test_f407*mosquitto_test.conf*') {
        Stop-Process -Id $wifiChild.ProcessId
    }
}
Stop-Process -Id $wifiSavedPid
Write-Output 'Stopped this project test server and its broker.'
