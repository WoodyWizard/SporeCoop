@echo off
rem SporeCoop uninstaller: run from the SporebinEP1 folder of SPORE (where install.bat was run)
cd /d "%~dp0"
if not exist SporeApp.exe (
  echo Put uninstall.bat into the SporebinEP1 folder of SPORE, next to SporeApp.exe, and run it again.
  pause
  exit /b 1
)
for %%I in ("%~dp0..") do set "SCROOT=%%~fI"
rem BUG-0851: the setup removes SporeCoop safely (the update lock, only SporeCoop's files, errors named)
if not exist SporeCoopSetup.exe goto nosetup
if exist mods\sporecoop-uninstall.log del mods\sporecoop-uninstall.log
start "" /wait SporeCoopSetup.exe /S /uninstall "/dir:%SCROOT%" "/log:%~dp0mods\sporecoop-uninstall.log"
if errorlevel 1 goto setupfailed
echo SporeCoop removed. The mods folder with your settings and logs was kept.
pause
exit /b 0
:setupfailed
echo SporeCoop could not be removed completely:
if exist mods\sporecoop-uninstall.log type mods\sporecoop-uninstall.log
pause
exit /b 1

:nosetup
rem Without SporeCoopSetup.exe (an antivirus may have removed it): the same rules here. steam_api.dll
rem goes back only when the current one is SporeCoop's loader (or missing) and steam_api_orig.dll is
rem the game's own file; a steam_api.dll that Steam or GOG put back stays.
set SCOURS=0
if not exist steam_api.dll set SCOURS=1
if exist steam_api.dll findstr /c:"sporekit-loader" steam_api.dll >nul 2>nul && set SCOURS=1
set SCLEFT=0
if "%SCOURS%"=="0" goto files
if not exist steam_api_orig.dll goto noorig
findstr /c:"sporekit-loader" steam_api_orig.dll >nul 2>nul && goto noorig
move /y steam_api_orig.dll steam_api.dll >nul 2>nul
if exist steam_api_orig.dll goto inuse
goto files
:noorig
echo The game's own steam_api.dll (steam_api_orig.dll) is missing, so SporeCoop was not removed.
echo Verify the game files in Steam or GOG Galaxy (or reinstall the game), then run uninstall.bat again.
pause
exit /b 1
:inuse
echo steam_api.dll cannot be replaced now: close Spore and run uninstall.bat again.
pause
exit /b 1

:files
for %%f in (sporekit.dll mods\coop.dll ..\Data\SporeCoop.package mods\sporekit\symbols\gog2016_922af7c7.sym mods\sporekit\symbols\march2017_dc04aee5.sym mods\sporekit\symbols\overrides_922af7c7.sym mods\sporekit\symbols\overrides_dc04aee5.sym mods\sporecoop.version) do (
  if exist "%%f" del /q "%%f" >nul 2>nul
  if exist "%%f" (
    echo Could not remove %%f: close Spore and run uninstall.bat again.
    set SCLEFT=1
  )
)
if "%SCOURS%"=="0" (
  echo steam_api.dll was not changed: it is not SporeCoop's loader of this version.
  echo If Spore still starts SporeCoop, run SporeCoopSetup.exe and press Uninstall.
)
if "%SCLEFT%"=="1" (
  pause
  exit /b 1
)
echo SporeCoop removed. The mods folder with your settings and logs was kept.
pause
exit /b 0
