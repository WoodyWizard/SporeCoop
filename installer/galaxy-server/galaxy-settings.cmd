rem Your galaxy server's settings. No update and no setup ever replaces this file.
rem Keep the form  set "NAME=value"  and plain letters, digits and spaces in the values.

rem The galaxy's name, as players read it:
set "GALAXY_NAME=My Lineages Galaxy"

rem Where the galaxy's planets are kept. Back this folder up to keep your galaxy:
set "GALAXY_WORLDS=%APPDATA%\SporeCoop\galaxy-server\worlds"

rem Updates. Before it starts, the server looks for a newer Lineages build:
rem   ask   it follows the game on this PC: when your game already has the new build the
rem         server updates by itself; when it has not, the server asks, and no answer
rem         within 10 seconds means no
rem   auto  it always updates without asking (for a server nobody watches)
rem   off   it does not look
set "GALAXY_UPDATE=ask"

rem More options for galaxy_server.exe (Read-me.txt lists them), for example: --max-players 32
set "GALAXY_OPTIONS="
