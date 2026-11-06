@echo off
setlocal
if not exist "%~dp0SporeApp.exe" goto :setup
if not exist "%~dp0SporeCoopSetup.exe" goto :setup
for %%I in ("%~dp0..") do set "GAME=%%~fI"
start "" /wait "%~dp0SporeCoopSetup.exe" /S /uninstall "/dir:%GAME%"
if errorlevel 1 goto :setup
echo SporeCoop removed. Your saves and settings are kept.
pause
exit /b 0
:setup
echo Close Spore, open SporeCoopSetup.exe, select Galactic Adventures and click Uninstall.
pause
exit /b 1
