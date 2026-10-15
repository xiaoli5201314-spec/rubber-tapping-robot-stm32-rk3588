$ErrorActionPreference = 'Stop'
$wifiPython = (Get-Command python -ErrorAction Stop).Source
$wifiServerScript = Join-Path $PSScriptRoot 'test_server.py'
$wifiOutput = Join-Path $PSScriptRoot 'server.log'
$wifiErrorOutput = Join-Path $PSScriptRoot 'server_error.log'
$wifiPidFile = Join-Path $PSScriptRoot 'server.pid'
$wifiConfig = Join-Path (Split-Path $PSScriptRoot) 'Inc\test_config.h'

if (Test-Path -LiteralPath $wifiPidFile) {
    $wifiOldPid = [int](Get-Content -LiteralPath $wifiPidFile)
    $wifiOldProcess = Get-CimInstance Win32_Process -Filter "ProcessId=$wifiOldPid"
    if ($wifiOldProcess -and $wifiOldProcess.CommandLine -like '*board_wifi_test_f407*test_server.py*') {
        Write-Output "Test server is already running (PID $wifiOldPid)."
        exit 0
    }
}

$wifiConfigText = Get-Content -LiteralPath $wifiConfig -Raw
$wifiMatch = [regex]::Match($wifiConfigText, '#define\s+TEST_SERVER_HOST\s+"([^"]+)"')
if (-not $wifiMatch.Success) { throw 'TEST_SERVER_HOST missing in config.' }
$wifiServerIp = $wifiMatch.Groups[1].Value
$wifiAddresses = @(Get-NetIPAddress -AddressFamily IPv4 | Select-Object -ExpandProperty IPAddress)
if ($wifiServerIp -notin $wifiAddresses) {
    throw "Computer no longer has firmware server IP $wifiServerIp. Update test_config.h and rebuild first."
}

$wifiArguments = '"{0}" --server-ip {1}' -f $wifiServerScript, $wifiServerIp
if (Get-NetTCPConnection -State Listen -LocalPort 1883 -ErrorAction SilentlyContinue) {
    $wifiArguments += ' --use-existing-broker'
}
$wifiProcess = Start-Process -FilePath $wifiPython -ArgumentList $wifiArguments `
    -WindowStyle Hidden -PassThru -RedirectStandardOutput $wifiOutput `
    -RedirectStandardError $wifiErrorOutput
$wifiProcess.Id | Set-Content -LiteralPath $wifiPidFile -Encoding ascii
$wifiDeadline = [DateTime]::UtcNow.AddSeconds(8)
$wifiReady = $false
while ([DateTime]::UtcNow -lt $wifiDeadline) {
    $wifiProcess.Refresh()
    if ($wifiProcess.HasExited) {
        if (Test-Path -LiteralPath $wifiErrorOutput) {
            Get-Content -LiteralPath $wifiErrorOutput
        }
        throw 'Test server failed to start.'
    }
    try {
        $wifiHealth = Invoke-WebRequest -Uri "http://127.0.0.1:18080/health" -TimeoutSec 1
        if ($wifiHealth.StatusCode -eq 200) { $wifiReady = $true; break }
    } catch {}
    Start-Sleep -Milliseconds 100
}
if (-not $wifiReady) { throw 'Test server did not become ready; see server_error.log.' }
Write-Output "Test server started (PID $($wifiProcess.Id)); HTTP 18080, MQTT 1883."
Write-Output "Report: $(Join-Path $PSScriptRoot 'result.json')"
