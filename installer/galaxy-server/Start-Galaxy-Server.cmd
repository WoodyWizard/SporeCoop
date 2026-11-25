@echo off
rem Lineages galaxy server (decision 8Y C). Change your settings in galaxy-settings.cmd, not here:
rem a newer SporeCoopSetup replaces this file and keeps galaxy-settings.cmd.
setlocal
set "GALAXY_NAME=My Lineages Galaxy"
set "GALAXY_WORLDS=%APPDATA%\SporeCoop\galaxy-server\worlds"
set "GALAXY_OPTIONS="
if exist "%~dp0galaxy-settings.cmd" call "%~dp0galaxy-settings.cmd"
rem The port is fixed: the game's Co-op window knows a galaxy by the port 47900 in the address.
set "GALAXY_PORT=47900"
if not exist "%GALAXY_WORLDS%" mkdir "%GALAXY_WORLDS%"
title Lineages Galaxy Server - %GALAXY_NAME% - close this window to stop it
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
