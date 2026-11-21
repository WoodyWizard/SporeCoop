# Start one muted QA Spore game from the QA copy (never the owner's own install).
#   tools/qa_win/launch.ps1 -Instance 9 -Profile DuoQAJiffHost [-Resolution 800x600]
# Owner's test settings (2026-11-12, docs/ROADMAP.md decision 11): small windows unless a test needs a size (Adventure
# clicks use 1440x1080); 5 FPS from the QA copy's mods/sporekit.ini (frame_ms=200). The QA copy needs steam_appid.txt
# (24720) next to SporeApp.exe so Steam does not relaunch the owner's install instead.
param([Parameter(Mandatory)][int]$Instance, [Parameter(Mandatory)][string]$Profile, [string]$Resolution = '800x600',
      [string]$Copy = $env:SPORECOOP_QA_COPY)
# (Windows PowerShell 5.1 leaves $PSScriptRoot empty inside a param default: the default copy is resolved here)
if (-not $Copy) { $Copy = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\..\out\release36\native' }
$exe = Join-Path (Resolve-Path $Copy) 'SporebinEP1\SporeApp.exe'
if (-not (Test-Path (Join-Path (Split-Path $exe) 'steam_appid.txt'))) { throw "no steam_appid.txt next to $exe (Steam would relaunch the owner's install)" }
# A Steam client that updated itself can leave HKCU\Software\Valve\Steam\ActiveProcess\pid on its gone bootstrapper:
# Spore's old steam_api then finds no Steam, SteamAPI_Init fails and the game quits in cSporeApp::Init (2026-11-15,
# docs/06-testing.md#testing-steam-update-qa-start). Point it at the running steam.exe (Steam rewrites it on its next start).
$ap = 'HKCU:\Software\Valve\Steam\ActiveProcess'
$steam = Get-Process steam -ErrorAction SilentlyContinue | Select-Object -First 1
$pidNow = (Get-ItemProperty $ap -ErrorAction SilentlyContinue).pid
if ($steam -and $pidNow -ne $steam.Id -and (-not $pidNow -or -not (Get-Process -Id $pidNow -ErrorAction SilentlyContinue))) { # (0: Get-Process finds the Idle process)
    Set-ItemProperty $ap -Name pid -Value $steam.Id -Type DWord
    "Steam's ActiveProcess pid $pidNow was stale: now $($steam.Id) (the running steam.exe)"
}
$env:SteamAppId = '24720'; $env:SteamGameId = '24720'
$p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru `
    -ArgumentList '-w', "-r:$Resolution", "-sporekit-profile:$Profile", "-sporekit-instance:$Instance"
"started instance $Instance pid $($p.Id) ($Resolution)"
