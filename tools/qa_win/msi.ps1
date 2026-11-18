# The MSI test PC over Tailscale SSH (`ssh msi`, docs/06-testing.md#testing-two-pc-msi).
#   tools/qa_win/msi.ps1 deploy     strip and copy build/coop.dll + build/sporekit.dll + the GOG 2024 symbols to its Steam Spore
#   tools/qa_win/msi.ps1 start      start Spore there in the logged-on desktop (scheduled task, profile MSIGuest)
#   tools/qa_win/msi.ps1 stop       close our test games there (profile MSIGuest only, never a Spore the owner started)
#   tools/qa_win/msi.ps1 tunnel     keep local TCP 47850 -> the MSI's console (127.0.0.1:47800); runs until closed
#   -Instance 2 (start, stop, tunnel): a second test game on the MSI (its own profile, console 47801, tunnel 47851)
# The first deploy of a day backs up the MSI's files to C:\temp\sporecoop-msi-backup-<date>.
param([Parameter(Mandatory)][ValidateSet('deploy', 'start', 'stop', 'tunnel')][string]$Action,
      [string]$Resolution = '1280x960', [string]$Profile = 'MSIGuest', [int]$Instance = 1)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$game = 'C:\Program Files (x86)\Steam\steamapps\common\spore 24720\SporebinEP1'

# runs a script on the MSI from a file (quotes in an ssh command line do not survive the Windows argument parsing)
function Invoke-Msi([string]$Name, [string]$Script) {
    $local = Join-Path $env:TEMP "msi-$Name.ps1"
    [IO.File]::WriteAllText($local, $Script)
    scp -o BatchMode=yes $local "msi:C:/temp/msi-$Name.ps1" | Out-Null
    ssh -o BatchMode=yes msi "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\msi-$Name.ps1"
}

switch ($Action) {
    'deploy' {
        if (-not $env:LLVM_MINGW) { throw 'set LLVM_MINGW to the llvm-mingw folder (for llvm-strip)' }
        $strip = Join-Path $env:LLVM_MINGW 'bin\llvm-strip.exe'
        $tmp = Join-Path $env:TEMP 'sporecoop-msi'; New-Item -ItemType Directory -Force $tmp | Out-Null
        & $strip -o "$tmp\coop.dll" "$root\build\coop.dll"; & $strip -o "$tmp\sporekit.dll" "$root\build\sporekit.dll"
        Copy-Item "$root\data\symbols\overrides_dc04aee5.sym" $tmp -Force
        ssh -o BatchMode=yes msi 'New-Item -ItemType Directory -Force C:\temp\sporecoop-msi-in | Out-Null'
        Copy-Item "$root\build\SporeCoop.package" $tmp -Force   # the UI images (a new one draws its fallback without it)
        scp -o BatchMode=yes "$tmp\coop.dll" "$tmp\sporekit.dll" "$tmp\overrides_dc04aee5.sym" "$tmp\SporeCoop.package" msi:C:/temp/sporecoop-msi-in/
        Invoke-Msi 'deploy' @"
`$d = '$game'
if (Get-Process SporeApp -ErrorAction SilentlyContinue) { throw 'Spore is running on the MSI: stop it first (msi.ps1 stop)' }
`$b = 'C:\temp\sporecoop-msi-backup-' + (Get-Date -Format yyyyMMdd)
if (-not (Test-Path "`$b\coop.dll")) { New-Item -ItemType Directory -Force `$b | Out-Null; Copy-Item "`$d\sporekit.dll","`$d\mods\coop.dll","`$d\mods\sporekit\symbols\overrides_dc04aee5.sym" `$b }
Copy-Item C:\temp\sporecoop-msi-in\sporekit.dll "`$d\sporekit.dll" -Force
Copy-Item C:\temp\sporecoop-msi-in\coop.dll "`$d\mods\coop.dll" -Force
Copy-Item C:\temp\sporecoop-msi-in\overrides_dc04aee5.sym "`$d\mods\sporekit\symbols\overrides_dc04aee5.sym" -Force
Copy-Item C:\temp\sporecoop-msi-in\SporeCoop.package (Join-Path (Split-Path `$d) 'Data\SporeCoop.package') -Force
'deployed: ' + (Get-Item "`$d\mods\coop.dll").Length + ' bytes'
"@
    }
    'start' {
        # a game started from the SSH shell has no desktop: a scheduled task runs it in the logged-on session. Its own
        # trigger lies in the past, so it never starts Spore by itself (a 23:59 trigger once did); /run starts it.
        $cmd = "@echo off`r`nset SteamAppId=24720`r`nset SteamGameId=24720`r`ncd /d `"$game`"`r`nstart `"`" SporeApp.exe -w -r:$Resolution -sporekit-profile:$Profile -sporekit-instance:$Instance`r`n"
        $tmp = Join-Path $env:TEMP "spore-msi-test$Instance.cmd"; [IO.File]::WriteAllText($tmp, $cmd)
        scp -o BatchMode=yes $tmp "msi:C:/temp/spore-msi-test$Instance.cmd" | Out-Null
        Invoke-Msi 'start' @"
schtasks /create /tn SporeCoopMSITest$Instance /tr C:\temp\spore-msi-test$Instance.cmd /sc once /sd 01/01/2020 /st 00:00 /it /f 2>`$null | Out-Null
schtasks /run /tn SporeCoopMSITest$Instance | Out-Null
Start-Sleep 15
`$c = Get-NetTCPConnection -State Listen -LocalPort $(47799 + $Instance) -ErrorAction SilentlyContinue
if (`$c) { 'console up, pid ' + `$c[0].OwningProcess } else { 'console not up yet' }
"@
    }
    'stop' {
        Invoke-Msi 'stop' @"
Get-CimInstance Win32_Process | Where-Object { `$_.Name -eq 'SporeApp.exe' -and `$_.CommandLine -match '-sporekit-profile:$Profile' } |
    ForEach-Object { Stop-Process -Id `$_.ProcessId -Force; 'stopped ' + `$_.ProcessId }
"@
    }
    'tunnel' { ssh -o BatchMode=yes -o ExitOnForwardFailure=yes -o ServerAliveInterval=30 -N -L "$(47849 + $Instance):127.0.0.1:$(47799 + $Instance)" msi }
}
