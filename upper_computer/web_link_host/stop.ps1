param(
    [ValidateRange(1, 65535)][int]$HttpPort = 18080,
    [ValidateRange(1, 65535)][int]$MqttPort = 1883
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$runtimeRoot = Join-Path $projectRoot 'runtime'
$statePath = Join-Path $runtimeRoot "state-$HttpPort-$MqttPort.json"
if (-not (Test-Path -LiteralPath $statePath)) {
    Write-Host 'No process manifest for this project. Nothing was stopped.'
    exit 0
}
try {
    $manifest = Get-Content -LiteralPath $statePath -Encoding UTF8 -Raw | ConvertFrom-Json
    if ($manifest.application -ne 'f407-web-link-host') { throw 'Unrecognized process manifest.' }
    $remaining = @()
    foreach ($record in @($manifest.processes | Sort-Object { if ($_.kind -eq 'server') { 0 } else { 1 } })) {
        $process = Get-Process -Id $record.id -ErrorAction SilentlyContinue
        if (-not $process) { continue }
        $details = Get-CimInstance Win32_Process -Filter ("ProcessId={0}" -f $record.id)
        $expectedMarker = if ($record.kind -eq 'server') { Join-Path $projectRoot 'server.py' } `
            elseif ($record.kind -eq 'broker') { Join-Path $runtimeRoot "mosquitto-$MqttPort.conf" } else { '' }
        $owned = $expectedMarker -and $record.marker -eq $expectedMarker -and
            [string]$process.StartTime.ToUniversalTime().Ticks -eq $record.startedTicks -and
            $details.ExecutablePath -eq $record.executable -and $details.CommandLine -and
            $details.CommandLine.Contains($expectedMarker)
        if (-not $owned) {
            Write-Host "Ownership check failed for PID $($record.id); left untouched." -ForegroundColor Yellow
            $remaining += $record
            continue
        }
        Stop-Process -Id $record.id -ErrorAction Stop
        $process.WaitForExit(5000) | Out-Null
        Write-Host "Stopped project $($record.kind), PID $($record.id)."
    }
    if ($remaining.Count -gt 0) {
        $manifest.processes = $remaining
        $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $statePath -Encoding UTF8
        exit 1
    }
    Remove-Item -LiteralPath $statePath
    Write-Host 'Project processes stopped. Reused external brokers were left running.'
    exit 0
} catch {
    Write-Host ("Stop failed: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
