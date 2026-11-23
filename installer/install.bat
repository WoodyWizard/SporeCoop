@echo off
rem SporeCoop installer: run from the SporebinEP1 folder of SPORE (GOG)
cd /d "%~dp0"
if not exist SporeApp.exe (
  echo Put install.bat and the SporeCoop folder into ...\SPORE\SporebinEP1 and run it again.
  pause
  exit /b 1
)
rem supported builds: GOG 2024 (dc04aee5...) and Steam (3010a413..., the GOG 2024 code under Steam's DRM),
rem INSTALL.txt section 2; GOG 2016 (922af7c7...) is not supported (BUG-1125)
set OLD2016=922af7c7908af2614b9caca2ffb1ec2353442022fd124866105724acaa24c26e
set WANT2=dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37
set WANT3=3010a413f9a4a05ee9c4de24756c1c94c7fb218f70cad29bcf5bc0b4abfa2466
for /f "usebackq delims=" %%h in (`powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 SporeApp.exe).Hash.ToLower()" 2^>nul`) do set HAVE=%%h
if defined HAVE if /i "%HAVE%"=="%OLD2016%" (
  echo This game version ^(GOG 2016^) is not supported by SporeCoop.
  echo Please update to the current GOG or Steam version of Spore, then run install.bat again.
  pause
  exit /b 1
)
if defined HAVE if /i not "%HAVE%"=="%WANT2%" if /i not "%HAVE%"=="%WANT3%" (
  echo Note: this SporeApp.exe is not one of the builds SporeCoop supports.
  echo It has not been tested yet - see INSTALL.txt, section 2.
  echo.
)
rem BUG-9310: the game's own steam_api.dll is kept as steam_api_orig.dll, never SporeCoop's loader (a backup that was
rem lost, e.g. to an antivirus, was replaced by the loader: it then loaded itself and the game did not start). A
rem steam_api_orig.dll that is the loader counts as missing. Every copy is checked.
set SCORIG=0
if exist steam_api_orig.dll set SCORIG=1
if "%SCORIG%"=="1" findstr /c:"sporekit-loader" steam_api_orig.dll >nul 2>nul && set SCORIG=0
if "%SCORIG%"=="1" goto haveorig
if not exist steam_api.dll goto noorig
findstr /c:"sporekit-loader" steam_api.dll >nul 2>nul && goto noorig
copy /y steam_api.dll steam_api_orig.dll >nul
if errorlevel 1 goto copyfailed
:haveorig
copy /y SporeCoop\steam_api.sporekit.dll steam_api.dll >nul
if errorlevel 1 goto copyfailed
copy /y SporeCoop\sporekit.dll sporekit.dll >nul
if errorlevel 1 goto copyfailed
if not exist mods mkdir mods
rem the player's settings stay (name, last address, [update] check=0): an .ini only when missing
xcopy /e /i /y /q /exclude:SporeCoop\ini.exclude SporeCoop\mods mods >nul
if errorlevel 1 goto copyfailed
set SCFAIL=0
for %%f in (SporeCoop\mods\*.ini) do if not exist "mods\%%~nxf" copy /y "%%f" "mods\%%~nxf" >nul || set SCFAIL=1
if "%SCFAIL%"=="1" goto copyfailed
copy /y SporeCoop\SporeCoop.package ..\Data\SporeCoop.package >nul
if errorlevel 1 goto copyfailed
echo SporeCoop installed. Start SPORE as usual.
echo Main menu: "Co-op" - host a game or join a friend. In game F9 opens the co-op window.
pause
exit /b 0

:noorig
echo The game's own steam_api.dll is missing: steam_api.dll is SporeCoop's loader and there is no
echo steam_api_orig.dll (the game's file) next to it, so SporeCoop was not installed.
echo Verify the game files in Steam or GOG Galaxy (or reinstall the game), then run install.bat again.
pause
exit /b 1
:copyfailed
echo SporeCoop could not copy its files, so it is not installed completely.
echo Close Spore and run install.bat again. If the game is in a folder that needs administrator rights
echo (Program Files), run install.bat as administrator, or use SporeCoopSetup.exe.
pause
exit /b 1
