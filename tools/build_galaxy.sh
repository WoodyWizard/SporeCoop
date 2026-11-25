#!/usr/bin/env bash
# The galaxy server (decision 5O A, server/galaxy): a small standalone program, no Spore, no SporeKit.
#   Windows (llvm-mingw, LLVM_MINGW set as for tools/build_windows.sh): bash tools/build_galaxy.sh -> build/galaxy_server.exe
#     and build/galaxy_update.exe, the server's update check (BUG-9378, server/galaxy/update_main.cpp + updater/)
#   Linux: bash tools/build_galaxy.sh -> build/galaxy_server (g++ or clang++)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
SRC=(server/galaxy/main.cpp mods/coop/net/udp.cpp mods/coop/net/galaxy_crypto.cpp)
if [[ -n "${LLVM_MINGW:-}" ]]; then
    CXX="$(cygpath -u "$LLVM_MINGW" 2>/dev/null || echo "$LLVM_MINGW")/bin/i686-w64-mingw32-clang++"
    "$CXX" -std=c++17 -O2 -static -o build/galaxy_server.exe "${SRC[@]}" -lws2_32 -liphlpapi
    echo "built build/galaxy_server.exe"
    # (the manifest: a program named "...update..." without one is refused by Windows as an installer needing an admin)
    "${CXX%clang++}windres" -i server/galaxy/update.rc -o build/galaxy_update_rc.o -Iserver/galaxy
    "$CXX" -std=c++20 -O2 -static -D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE -o build/galaxy_update.exe \
        server/galaxy/update_main.cpp updater/winupdate.cpp updater/pack.cpp updater/sha256.cpp build/galaxy_update_rc.o \
        -lwinhttp -lws2_32
    echo "built build/galaxy_update.exe"
else
    "${CXX:-c++}" -std=c++17 -O2 -o build/galaxy_server "${SRC[@]}"
    echo "built build/galaxy_server"
fi
