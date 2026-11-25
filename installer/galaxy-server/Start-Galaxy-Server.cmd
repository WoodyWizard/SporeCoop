@echo off
rem Lineages galaxy server (decision 8Y C). Change your settings in galaxy-settings.cmd, not here:
rem an update replaces this file and keeps galaxy-settings.cmd.
setlocal
set "GALAXY_NAME=My Lineages Galaxy"
set "GALAXY_WORLDS=%APPDATA%\SporeCoop\galaxy-server\worlds"
set "GALAXY_OPTIONS="
set "GALAXY_UPDATE=ask"
if exist "%~dp0galaxy-settings.cmd" call "%~dp0galaxy-settings.cmd"
rem The port is fixed: the game's Co-op window knows a galaxy by the port 47900 in the address.
set "GALAXY_PORT=47900"
title Lineages Galaxy Server - %GALAXY_NAME% - close this window to stop it

rem The update check before the server runs (BUG-9378). "updated" is this file's own restart after an update.
rem Decision 8Z: the server follows the game on this PC. The check answers 11 when a newer build is out and the game
rem here already has it (update, no question), 10 when the game here is still on the old build (ask; no answer
rem means no, so everybody can go on playing), 0 when there is nothing to do.
if /i "%~1"=="updated" goto run
if /i "%GALAXY_UPDATE%"=="off" goto run
if not exist "%~dp0galaxy_update.exe" goto run
"%~dp0galaxy_update.exe" --check
if errorlevel 11 goto update
if not errorlevel 10 goto run
if /i "%GALAXY_UPDATE%"=="auto" goto update
choice /C YN /T 10 /D N /M "Update the galaxy server now (N by itself in 10 seconds)"
if errorlevel 2 goto run
:update
rem One line on purpose: the update replaces this file, so nothing after the update is read from the old one.
"%~dp0galaxy_update.exe" --apply & "%~f0" updated

:run
if not exist "%GALAXY_WORLDS%" mkdir "%GALAXY_WORLDS%"
echo.
echo Lineages galaxy server "%GALAXY_NAME%", UDP port %GALAXY_PORT%.
echo Its planets are kept in: %GALAXY_WORLDS%
echo Players type this PC's address and the port into the Co-op window's Join,
echo for example 192.168.1.5:%GALAXY_PORT% (you yourself: 127.0.0.1:%GALAXY_PORT%).
echo Close this window to stop the server. More: Read-me.txt in this folder.
echo.
"%~dp0galaxy_server.exe" --worlds "%GALAXY_WORLDS%" %GALAXY_OPTIONS% %GALAXY_PORT% "%GALAXY_NAME%"
echo.
echo The galaxy server has stopped.
pause
