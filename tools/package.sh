#!/usr/bin/env bash
# Build a distributable zip for players (Windows or Wine): out/dist/SporeCoop-<ver>.zip
# Contents are extracted into SporebinEP1 and installed with install.bat.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VER="$(grep -oP 'define COOP_VERSION "\K[^"]+' "$ROOT/mods/coop/coop.cpp")"
STAGE="$ROOT/out/dist/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/SporeCoop/mods/sporekit/symbols"
cp "$ROOT/build/steam_api.dll" "$STAGE/SporeCoop/steam_api.sporekit.dll"
cp "$ROOT/build/sporekit.dll" "$STAGE/SporeCoop/sporekit.dll"
cp "$ROOT/build/coop.dll" "$STAGE/SporeCoop/mods/coop.dll"
cp "$ROOT"/data/symbols/*.sym "$STAGE/SporeCoop/mods/sporekit/symbols/"
cp "$ROOT"/data/config/*.ini "$STAGE/SporeCoop/mods/"
cp "$ROOT/build/SporeCoop.package" "$STAGE/SporeCoop/SporeCoop.package"  # menu icon (Spore loads Data\*.package)

# install.bat / uninstall.bat (installer/): install.bat keeps the player's .ini files (BUG-0080)
cp "$ROOT/installer/install.bat" "$ROOT/installer/uninstall.bat" "$STAGE/"
cp "$ROOT/installer/ini.exclude" "$STAGE/SporeCoop/ini.exclude"  # xcopy /exclude: names with ".ini"

cat > "$STAGE/README-SporeCoop.txt" <<EOF
SporeCoop $VER - co-op for every stage of SPORE (GOG "SPORE Collection" and Steam)
https://github.com/WoodyWizard/SporeCoop
Discord (players, help, reports): https://discord.gg/7Yre7u4b8x

Install: extract this zip into SPORE\\SporebinEP1 and run install.bat (step by step: INSTALL.txt).
Supported builds of SporebinEP1\\SporeApp.exe: GOG 2024 (sha256 dc04aee5...), Steam (3010a413...: the
same game code under Steam's DRM) and, until 0.7.x, GOG 2016 (922af7c7...).
Tested: GOG 2016 and 2024 on Linux (Wine), GOG 2024 on Windows 11, Steam on Linux (Proton); EA app and disc not tested
(the full list: INSTALL.txt, section 2). All players need the same game build.

Host: main menu -> Co-op -> name -> Host, then load or start a game. The window shows the
addresses for friends (over the internet forward UDP port 47810 or use a VPN such as
ZeroTier / Radmin VPN / Tailscale).
Guest: main menu -> Co-op -> name -> click the host under "Games on your network" or type
its address -> Join. The host's world arrives and loads by itself.
In game F9 opens the lobby: players, stage, ping, ready, chat, "Send my world", "Leave".
Evolution (editor, next stage) is decided by a vote: everybody presses "I'm ready".
Cheat console (Ctrl+Shift+C) works too: coop host | coop join <ip> | coop leave.
EOF
# the toolchain the Makefile uses (LLVM_MINGW ?= ... there), unless set in the environment
LLVM_MINGW="${LLVM_MINGW:-$(sed -n 's/^LLVM_MINGW ?= //p' "$ROOT/Makefile" | sed 's/\$(HOME)/$HOME/; s|\$HOME|'"$HOME"'|')}"
STRIP="$LLVM_MINGW/bin/llvm-strip"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
cp "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/THIRD_PARTY_NOTICES.txt"
cp "$ROOT/third_party/minhook/LICENSE.txt" "$STAGE/SporeCoop/MinHook-LICENSE.txt"
cp "$ROOT/docs/INSTALL.md" "$STAGE/INSTALL.txt"
"$STRIP" "$STAGE/SporeCoop/sporekit.dll" "$STAGE/SporeCoop/mods/coop.dll" "$STAGE/SporeCoop/steam_api.sporekit.dll"
# the update pack (in-game updates and the setup) and the setup itself, also next to the zip
python3 "$ROOT/tools/mkpack.py" "$STAGE" "$VER" "$STAGE/SporeCoop-$VER.pack" >/dev/null
cp "$ROOT/build/SporeCoopSetup.exe" "$STAGE/SporeCoopSetup.exe"
"$STRIP" "$STAGE/SporeCoopSetup.exe"
# BUG-0784: every binary of the release carries a VERSIONINFO of this version (after the strip)
python3 "$ROOT/tools/pe_version.py" --expect "$VER" "$STAGE/SporeCoopSetup.exe" "$STAGE/SporeCoop/steam_api.sporekit.dll" \
    "$STAGE/SporeCoop/sporekit.dll" "$STAGE/SporeCoop/mods/coop.dll" >&2
cp "$STAGE/SporeCoop-$VER.pack" "$STAGE/SporeCoopSetup.exe" "$ROOT/out/dist/"
sed -i 's/\r*$/\r/' "$STAGE"/*.bat "$STAGE"/*.txt "$STAGE/SporeCoop/ini.exclude"  # Windows line endings for cmd.exe and Notepad
(cd "$STAGE" && rm -f "../SporeCoop-$VER.zip" && zip -qr "../SporeCoop-$VER.zip" .)
echo "$ROOT/out/dist/SporeCoop-$VER.zip"
