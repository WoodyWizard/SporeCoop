# Real mouse input on the MSI test PC's desktop (the console's move / click is a virtual cursor that lasts 5 s and does
# not reach the game's own hover over a background window: every creature rated class 0, 2026-11-13).
#   tools/qa_win/msi_input.ps1 -X 556 -Y 428 [-Click | -RightClick] [-Instance 1]
# X / Y are client coordinates of the test game's window. The game window is brought to the front first, the pointer
# moves there in a few steps (the game reads a move into the world), then an optional left click. Runs as a one-shot
# scheduled task in the logged-on session (an SSH shell has no desktop), like msi.ps1 start. The MSI only: on this PC
# use tools/qa_win/hover.ps1 and the owner's OK.
param([Parameter(Mandatory)][int]$X, [Parameter(Mandatory)][int]$Y, [switch]$Click, [switch]$RightClick, [int]$Instance = 1)
$ErrorActionPreference = 'Stop'
$port = 47799 + $Instance
$clickLine = if ($Click) { '[MsiIn]::mouse_event(2,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 90; [MsiIn]::mouse_event(4,0,0,0,[UIntPtr]::Zero)' }
             elseif ($RightClick) { '[MsiIn]::mouse_event(8,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 90; [MsiIn]::mouse_event(0x10,0,0,0,[UIntPtr]::Zero)' } else { '' }
$inner = @"
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Drawing; using System.Runtime.InteropServices;
public static class MsiIn {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref Point p);
}
'@
`$gp = (Get-NetTCPConnection -State Listen -LocalPort $port).OwningProcess | Select-Object -First 1
`$h = (Get-Process -Id `$gp).MainWindowHandle
[MsiIn]::SetForegroundWindow(`$h) | Out-Null
Start-Sleep -Milliseconds 300
`$p = New-Object System.Drawing.Point $X, $Y
[MsiIn]::ClientToScreen(`$h, [ref]`$p) | Out-Null
for (`$i = 6; `$i -ge 0; `$i--) { [MsiIn]::SetCursorPos(`$p.X - 6 * `$i, `$p.Y + 4 * `$i) | Out-Null; Start-Sleep -Milliseconds 80 }
Start-Sleep -Milliseconds 400
$clickLine
"@
$local = Join-Path $env:TEMP 'msi-input-inner.ps1'
[IO.File]::WriteAllText($local, $inner)
scp -o BatchMode=yes $local 'msi:C:/temp/msi-input-inner.ps1' | Out-Null
$runner = Join-Path $env:TEMP 'msi-input.ps1'
[IO.File]::WriteAllText($runner, @"
schtasks /create /tn SporeCoopMSIInput /tr "powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File C:\temp\msi-input-inner.ps1" /sc once /sd 01/01/2020 /st 00:00 /it /f 2>`$null | Out-Null
schtasks /run /tn SporeCoopMSIInput | Out-Null
Start-Sleep 3
'input sent'
"@)
scp -o BatchMode=yes $runner 'msi:C:/temp/msi-input.ps1' | Out-Null
ssh -o BatchMode=yes msi 'powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\msi-input.ps1'
