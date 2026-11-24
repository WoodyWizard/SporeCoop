#!/usr/bin/env bash
# The galaxy server (decision 5O A, server/galaxy): a small standalone program, no Spore, no SporeKit.
#   Windows (llvm-mingw, LLVM_MINGW set as for tools/build_windows.sh): bash tools/build_galaxy.sh -> build/galaxy_server.exe
#   Linux: bash tools/build_galaxy.sh -> build/galaxy_server (g++ or clang++)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
SRC=(server/galaxy/main.cpp mods/coop/net/udp.cpp mods/coop/net/galaxy_crypto.cpp)
if [[ -n "${LLVM_MINGW:-}" ]]; then
    CXX="$(cygpath -u "$LLVM_MINGW" 2>/dev/null || echo "$LLVM_MINGW")/bin/i686-w64-mingw32-clang++"
    "$CXX" -std=c++17 -O2 -static -o build/galaxy_server.exe "${SRC[@]}" -lws2_32 -liphlpapi
    echo "built build/galaxy_server.exe"
else
    "${CXX:-c++}" -std=c++17 -O2 -o build/galaxy_server "${SRC[@]}"
    echo "built build/galaxy_server"
fi
