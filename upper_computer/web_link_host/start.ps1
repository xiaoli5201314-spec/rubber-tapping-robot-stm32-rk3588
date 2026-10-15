param(
    [ValidateRange(1, 65535)][int]$HttpPort = 18080,
    [ValidateRange(1, 65535)][int]$MqttPort = 1883,
    [switch]$NoBrowser
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$runtimeRoot = Join-Path $projectRoot 'runtime'
$serverPath = Join-Path $projectRoot 'server.py'
$statePath = Join-Path $runtimeRoot "state-$HttpPort-$MqttPort.json"
$createdManifest = $false
$manifest = @{ application = 'f407-web-link-host'; httpPort = $HttpPort; mqttPort = $MqttPort; processes = @() }

function Save-Manifest {
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $statePath -Encoding UTF8
    $script:createdManifest = $true
}

function Remember-Process($process, $kind, $marker, $executable) {
    $process.Refresh()
    $manifest.processes += @{
        kind = $kind
        id = $process.Id
        startedTicks = [string]$process.StartTime.ToUniversalTime().Ticks
        executable = [IO.Path]::GetFullPath($executable)
        marker = $marker
    }
    Save-Manifest
}

function List-Addresses {
    Write-Host "Web: http://127.0.0.1:$HttpPort/"
    $addresses = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -ne '127.0.0.1' -and $_.AddressState -ne 'Invalid' } |
        Select-Object -Unique IPAddress, InterfaceAlias)
    foreach ($address in $addresses) {
        Write-Host ("Web: http://{0}:{1}/  [{2}]" -f $address.IPAddress, $HttpPort, $address.InterfaceAlias)
    }
    Write-Host 'STM32 uses its Wi-Fi gateway as the server address. No hotspot IP is assumed.'
}

try {
    $httpListeners = @(Get-NetTCPConnection -State Listen -LocalPort $HttpPort -ErrorAction SilentlyContinue)
    if ($httpListeners.Count -gt 0) {
        if (Test-Path -LiteralPath $statePath) {
            $existing = Get-Content -LiteralPath $statePath -Encoding UTF8 -Raw | ConvertFrom-Json
            $health = Invoke-RestMethod -Uri "http://127.0.0.1:$HttpPort/health" -TimeoutSec 3
            $ownedServer = @($existing.processes | Where-Object { $_.kind -eq 'server' -and $_.id -eq $health.pid })
            if ($health.application -eq 'f407-web-link-host' -and $ownedServer.Count -eq 1) {
                $running = Get-Process -Id $health.pid -ErrorAction Stop
                $details = Get-CimInstance Win32_Process -Filter ("ProcessId={0}" -f $health.pid)
                if ([string]$running.StartTime.ToUniversalTime().Ticks -eq $ownedServer[0].startedTicks -and
                    $details.ExecutablePath -eq $ownedServer[0].executable -and
                    $ownedServer[0].marker -eq $serverPath -and $details.CommandLine.Contains($serverPath)) {
                    Write-Host 'This project is already running.'
                    List-Addresses
                    if (-not $NoBrowser) { Start-Process "http://127.0.0.1:$HttpPort/" }
                    exit 0
                }
            }
        }
        throw "HTTP port $HttpPort is occupied by another process. Nothing was stopped."
    }
    if (Test-Path -LiteralPath $statePath) {
        throw 'A previous process manifest exists. Run stop.ps1 first; no process was taken over.'
    }
    $pythonPath = $null
    $pythonCandidates = @()
    $commandPython = Get-Command python -ErrorAction SilentlyContinue
    if ($commandPython) { $pythonCandidates += $commandPython.Source }
    foreach ($candidate in $pythonCandidates) {
        if (Test-Path -LiteralPath $candidate) {
            & $candidate -c 'import paho.mqtt.client' 2>$null
            if ($LASTEXITCODE -eq 0) { $pythonPath = $candidate; break }
        }
    }
    if (-not $pythonPath) { throw 'Python with paho-mqtt is required. Install paho-mqtt in your Python environment.' }
    New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
    $mqttListeners = @(Get-NetTCPConnection -State Listen -LocalPort $MqttPort -ErrorAction SilentlyContinue)
    if ($mqttListeners.Count -gt 0) {
        $publicListener = @($mqttListeners | Where-Object { $_.LocalAddress -eq '0.0.0.0' })
        if ($publicListener.Count -eq 0) { throw "MQTT port $MqttPort has no all-IPv4 listener. Existing process left untouched." }
        & $pythonPath $serverPath --check-broker --mqtt-port $MqttPort
        if ($LASTEXITCODE -ne 0) { throw "Existing MQTT port $MqttPort is incompatible. Existing process left untouched." }
        Write-Host "Reusing compatible existing MQTT broker on $MqttPort. stop.ps1 will leave it running."
        Save-Manifest
    } else {
        $brokerPath = Join-Path $env:ProgramFiles 'mosquitto\mosquitto.exe'
        if (-not (Test-Path -LiteralPath $brokerPath)) {
            $brokerCommand = Get-Command mosquitto -ErrorAction SilentlyContinue
            if (-not $brokerCommand) { throw 'Mosquitto executable was not found.' }
            $brokerPath = $brokerCommand.Source
        }
        $configPath = Join-Path $runtimeRoot "mosquitto-$MqttPort.conf"
        @("listener $MqttPort 0.0.0.0", 'allow_anonymous true', 'persistence false') |
            Set-Content -LiteralPath $configPath -Encoding ASCII
        $brokerProcess = Start-Process -FilePath $brokerPath -ArgumentList @('-c', ('"{0}"' -f $configPath)) `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $runtimeRoot "broker-$MqttPort.log") `
            -RedirectStandardError (Join-Path $runtimeRoot "broker-$MqttPort-error.log")
        Remember-Process $brokerProcess 'broker' $configPath $brokerPath
        $ready = $false
        for ($attempt = 0; $attempt -lt 15; $attempt++) {
            $brokerProcess.Refresh()
            if ($brokerProcess.HasExited) { throw 'Project broker exited; inspect runtime broker logs.' }
            $listener = @(Get-NetTCPConnection -State Listen -LocalPort $MqttPort -ErrorAction SilentlyContinue |
                Where-Object { $_.OwningProcess -eq $brokerProcess.Id })
            if ($listener.Count -gt 0) { $ready = $true; break }
            Start-Sleep -Milliseconds 200
        }
        if (-not $ready) { throw 'Project broker did not open its listener.' }
        & $pythonPath $serverPath --check-broker --mqtt-port $MqttPort
        if ($LASTEXITCODE -ne 0) { throw 'Project broker did not accept MQTT subscription.' }
    }
    $arguments = @('-u', ('"{0}"' -f $serverPath), '--bind', '0.0.0.0',
        '--http-port', $HttpPort, '--mqtt-port', $MqttPort)
    $serverProcess = Start-Process -FilePath $pythonPath -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $runtimeRoot "server-$HttpPort.log") `
        -RedirectStandardError (Join-Path $runtimeRoot "server-$HttpPort-error.log")
    Remember-Process $serverProcess 'server' $serverPath $pythonPath
    $ready = $false
    for ($attempt = 0; $attempt -lt 25; $attempt++) {
        $serverProcess.Refresh()
        if ($serverProcess.HasExited) { throw 'Project web host exited; inspect runtime server logs.' }
        try {
            $health = Invoke-RestMethod -Uri "http://127.0.0.1:$HttpPort/health" -TimeoutSec 1
            $status = Invoke-RestMethod -Uri "http://127.0.0.1:$HttpPort/api/status" -TimeoutSec 1
            if ($health.application -eq 'f407-web-link-host' -and $health.pid -eq $serverProcess.Id -and $status.brokerConnected) {
                $ready = $true; break
            }
        } catch { }
        Start-Sleep -Milliseconds 200
    }
    if (-not $ready) { throw 'Host did not become ready; inspect runtime logs.' }
    Write-Host 'Host ready. Waiting for new physical STM32 heartbeats.'
    List-Addresses
    Write-Host "HTTP $HttpPort and MQTT $MqttPort listen on all IPv4 interfaces."
    Write-Host 'If the board cannot connect, allow these programs/ports in Windows Firewall on the hotspot network.'
    Write-Host "Logs and ownership manifest: $runtimeRoot"
    if (-not $NoBrowser) { Start-Process "http://127.0.0.1:$HttpPort/" }
    exit 0
} catch {
    Write-Host ("Start failed: " + $_.Exception.Message) -ForegroundColor Red
    if ($manifest.processes.Count -gt 0 -and (Test-Path -LiteralPath $statePath)) {
        & (Join-Path $projectRoot 'stop.ps1') -HttpPort $HttpPort -MqttPort $MqttPort
    } elseif ($createdManifest -and (Test-Path -LiteralPath $statePath)) {
        Remove-Item -LiteralPath $statePath
    }
    exit 1
}
