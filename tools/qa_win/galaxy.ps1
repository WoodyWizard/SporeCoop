# The galaxy server on this PC in a visible console window (owner 2026-11-17: "can we give it a visible cmd prompt so I
# know its running or not?"), its lines also in out\galaxy\galaxy.log (UTF-8).
#   tools/qa_win/galaxy.ps1 start [-Name "Lineages Alpha Galaxy"] [-Port 47900] [-Worlds out\galaxy\worlds] [-Extra '--warm-handover']
#   tools/qa_win/galaxy.ps1 stop       closes it (the window too)
#   tools/qa_win/galaxy.ps1 status     running or not, the last lines of the log
# Runs build\galaxy_server.exe: the Windows firewall rule for this PC names that path.
param([Parameter(Mandatory)][ValidateSet('start', 'stop', 'status')][string]$Action,
      [string]$Name = 'Lineages Alpha Galaxy', [int]$Port = 47900, [string]$Worlds = 'out\galaxy\worlds', [string]$Extra = '')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\..')).Path
$exe = Join-Path $root 'build\galaxy_server.exe'
$log = Join-Path $root 'out\galaxy\galaxy.log'
$running = Get-Process galaxy_server -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }
switch ($Action) {
    'start' {
        if ($running) { "already running (pid $($running.Id))"; return }
        New-Item -ItemType Directory -Force (Join-Path $root $Worlds) | Out-Null
        if (Test-Path $log) { Move-Item -Force $log ($log -replace '\.log$', '.previous.log') }
        $cmd = "`$host.UI.RawUI.WindowTitle = 'SporeCoop Galaxy Server - $Name (port $Port)'; " +
               "Set-Location '$root'; Write-Host 'SporeCoop Galaxy Server: close this window to stop it.' -ForegroundColor Cyan; " +
               "& '$exe' --worlds '$Worlds' $Extra $Port '$Name' 2>&1 | ForEach-Object { `$_; Add-Content -LiteralPath '$log' -Value `$_ -Encoding UTF8 }"
        Start-Process powershell -ArgumentList '-NoProfile', '-NoExit', '-Command', $cmd | Out-Null
        Start-Sleep -Seconds 2
        $p = Get-Process galaxy_server -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }
        if ($p) { "started (pid $($p.Id)), window 'SporeCoop Galaxy Server'" } else { 'did not start: see the window' }
    }
    'stop' {
        if (-not $running) { 'not running'; return }
        $running | ForEach-Object { Stop-Process -Id $_.Id -Force }
        # (the window's PowerShell, found by its command line: on Windows 11 the window belongs to Windows Terminal)
        Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" |
            Where-Object { $_.CommandLine -like '*SporeCoop Galaxy Server*' -and $_.ProcessId -ne $PID } |
            ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
        'stopped'
    }
    'status' {
        if ($running) { "running (pid $($running.Id))" } else { 'not running' }
        if (Test-Path $log) { Get-Content $log -Tail 5 }
    }
}
