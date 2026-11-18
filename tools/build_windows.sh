#!/usr/bin/env bash
# Build loader, SporeKit and coop.dll on a Windows host (Git Bash) without make.
# Same flags as the Makefile. Needs the llvm-mingw Windows toolchain
# (https://github.com/mstorsjo/llvm-mingw/releases, llvm-mingw-<ver>-ucrt-x86_64.zip):
#   LLVM_MINGW=/c/path/to/llvm-mingw-20261006-ucrt-x86_64 tools/build_windows.sh [loader|sporekit|coop|all]
set -euo pipefail
cd "$(dirname "$0")/.."
: "${LLVM_MINGW:?set LLVM_MINGW to the llvm-mingw folder}"
CC="$LLVM_MINGW/bin/i686-w64-mingw32-clang"
CXX="$LLVM_MINGW/bin/i686-w64-mingw32-clang++"
B=build
COMMON="-O2 -g -Wall -Wno-unused-function -Wno-missing-braces -D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE"
LDDLL="-shared -static -Wl,--kill-at -Wl,--enable-stdcall-fixup"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
what="${1:-all}"

# stale <obj> <src>: the object must be built again. Since 2026-11-12 (roadmap 0a) the compiler writes the headers
# each object includes (-MMD -MP: build/obj/<src>.d); a header newer than the object rebuilds it too. Before, only the
# source's own date counted: a changed header (a struct layout) left other objects on the old layout and both test
# guests faulted (BUG-9098 notes). No depfile, or one older than its object (written by another build): built again
# once. The compiler writes CRLF line ends here; they are stripped before the header names are read.
stale() {
  local obj=$1 src=$2 dep="${1%.o}.d" w
  [[ -n "${FORCE:-}" || ! -f "$obj" || ! -f "$dep" || "$src" -nt "$obj" || "$obj" -nt "$dep" ]] && return 0
  for w in $(sed -e 's/\r$//' -e 's/\\$//' -e 's/^[^:]*://' "$dep"); do
    [[ -e "$w" && "$w" -nt "$obj" ]] && return 0
  done
  return 1
}

# compile <c|cpp> <src...>: objects into build/obj, in parallel, only when the source or a header it includes is newer
compile() {
  local lang=$1; shift
  local pids=() src obj
  for src in "$@"; do
    obj="$B/obj/${src%.*}.o"
    mkdir -p "$(dirname "$obj")"
    if ! stale "$obj" "$src"; then continue; fi
    # the depfile is touched after the object, so "object newer than its depfile" always means another build wrote it
    if [[ $lang == c ]]; then
      ( "$CC" $COMMON -MMD -MP -Ithird_party/minhook/include -c "$src" -o "$obj" && touch "${obj%.o}.d" ) &
    else
      ( "$CXX" $COMMON -MMD -MP -std=c++20 -Ithird_party/minhook/include -Isdk/include -c "$src" -o "$obj" && touch "${obj%.o}.d" ) &
    fi
    pids+=($!)
    if (( ${#pids[@]} >= JOBS )); then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
  done
  for p in "${pids[@]}"; do wait "$p"; done
}
objs() { for s in "$@"; do echo "$B/obj/${s%.*}.o"; done; }

mkdir -p "$B"
if [[ $what == all || $what == loader ]]; then
  "$CC" $COMMON -shared -o "$B/steam_api.dll" loader/loader.c loader/steam_api.def -Wl,--kill-at
  echo "built $B/steam_api.dll"
fi
if [[ $what == all || $what == sporekit ]]; then
  MH=(third_party/minhook/src/hook.c third_party/minhook/src/buffer.c third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde32.c)
  KIT=(sporekit/*.cpp)
  compile c "${MH[@]}"
  compile cpp "${KIT[@]}"
  "$CXX" $LDDLL -o "$B/sporekit.dll" $(objs "${KIT[@]}") $(objs "${MH[@]}") \
    -lws2_32 -luser32 -lshell32 -ld3d9 -Wl,--out-implib,"$B/libsporekit.a"
  echo "built $B/sporekit.dll"
fi
if [[ $what == all || $what == coop ]]; then
  COOP=(mods/coop/*.cpp mods/coop/net/*.cpp updater/*.cpp)
  compile cpp "${COOP[@]}"
  "$CXX" $LDDLL -o "$B/coop.dll" $(objs "${COOP[@]}") -lws2_32 -luser32 -liphlpapi -lwinhttp -lshell32
  echo "built $B/coop.dll"
fi
