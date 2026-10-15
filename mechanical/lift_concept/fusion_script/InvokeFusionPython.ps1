param(
    [Parameter(Mandatory = $true)][string]$Command,
    [int]$PauseMilliseconds = 1000,
    [switch]$Screenshot
)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ConceptWindow {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(
        uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
}
'@
[void][ConceptWindow]::SetProcessDPIAware()
Add-Type -AssemblyName System.Windows.Forms
$cadProcess = Get-Process Fusion360 | Where-Object { $_.MainWindowHandle -ne 0 } |
    Select-Object -First 1
if (-not $cadProcess) { throw 'Fusion is not open.' }
[void][ConceptWindow]::SetForegroundWindow($cadProcess.MainWindowHandle)
Start-Sleep -Milliseconds 350
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
# The expanded Text Commands panel is already visible and set to Python.
[void][ConceptWindow]::SetCursorPos(
    [int]($screen.Width * 0.25), [int]($screen.Height * 0.93))
[ConceptWindow]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
[ConceptWindow]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
[System.Windows.Forms.SendKeys]::SendWait('{ESC}')
$clipboardImage = [System.Windows.Forms.Clipboard]::GetImage()
$clipboardText = [System.Windows.Forms.Clipboard]::GetText()
try {
    # Paste avoids Chinese IME interpreting Python identifier keystrokes.
    [System.Windows.Forms.Clipboard]::SetText($Command)
    [System.Windows.Forms.SendKeys]::SendWait('^a')
    [System.Windows.Forms.SendKeys]::SendWait('^v')
    Start-Sleep -Milliseconds 150
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
} finally {
    if ($clipboardImage) {
        [System.Windows.Forms.Clipboard]::SetImage($clipboardImage)
        $clipboardImage.Dispose()
    } elseif ($clipboardText) {
        [System.Windows.Forms.Clipboard]::SetText($clipboardText)
    } else {
        [System.Windows.Forms.Clipboard]::Clear()
    }
}
Start-Sleep -Milliseconds $PauseMilliseconds
if ($Screenshot) {
    Write-Warning 'Use the Fusion viewport export command to save a local screenshot.'
}
