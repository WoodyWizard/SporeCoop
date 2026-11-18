#!/bin/bash
# Builds (or refreshes the names of) the private Ghidra project of one game build.
#
#   tools/ghidra/mkproject.sh [--build 2016|2024] [--exe <SporeApp.exe>]   full import + analysis
#   tools/ghidra/mkproject.sh [--build 2016|2024] --names                  re-apply the .sym names
#
# Full build: import + Ghidra auto-analysis (about 15 min), then tools/ghidra/SporeSetup.java:
# extra functions at call rel32 / data pointer targets, and the names and notes of every
# data/symbols/*_<sha8>.sym of this checkout (base files first, overrides win). The project is
# built next to the old one and swapped in at the end, so q.sh keeps working meanwhile.
# Projects: <main checkout>/out/ghidra-full (2016), out/ghidra-2024 (2024). Never writes the exe.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BUILD=2016; EXE=""; NAMES=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build) BUILD="$2"; shift 2 ;;
        --exe) EXE="$2"; shift 2 ;;
        --names) NAMES=1; shift ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
gh_build "$BUILD"
EXE="${EXE:-$GH_EXE}"

SYMS=()
for f in "$GH_ROOT"/data/symbols/*_"$GH_SHA".sym; do
    [[ "$(basename "$f")" == overrides_* ]] || SYMS+=("$f")
done
for f in "$GH_ROOT"/data/symbols/overrides_*_"$GH_SHA".sym "$GH_ROOT"/data/symbols/overrides_"$GH_SHA".sym; do
    [[ -f "$f" ]] && SYMS+=("$f")
done
[[ ${#SYMS[@]} -gt 0 ]] || { echo "no data/symbols/*_$GH_SHA.sym" >&2; exit 1; }

mkdir -p "$(dirname "$GH_PROJ")"
LOGDIR="$GH_MAIN/out/ghidra-logs"; mkdir -p "$LOGDIR"
LOG="$LOGDIR/mkproject-$BUILD-$(date +%Y%m%d-%H%M%S).log"
echo "log: $LOG"

if [[ $NAMES == 1 ]]; then
    [[ -f "$GH_PROJ/spore.gpr" ]] || { echo "no project $GH_PROJ" >&2; exit 1; }
    exec 9>"$GH_PROJ.lock"; flock -x 9
    gh_ah "$GH_PROJ" spore -process SporeApp.exe -noanalysis -scriptPath "$GH_HERE" \
        -postScript SporeSetup.java nodisc "${SYMS[@]}" > "$LOG" 2>&1
    grep -E 'SporeSetup.java> ' "$LOG" | sed 's/.*SporeSetup.java> //'
    exit 0
fi

[[ -f "$EXE" ]] || { echo "no exe $EXE (pass --exe)" >&2; exit 1; }
sha="$(sha256sum "$EXE" | cut -c1-8)"
# SPORE_GHIDRA_ANY_EXE=1: an exe of the same code with another hash (Windows: the Steam exe's image dumped from a
# running game, its relocations undone, docs/05-re-notes.md)
if [[ "$sha" != "$GH_SHA" && "${SPORE_GHIDRA_ANY_EXE:-0}" != 1 ]]; then
    echo "$EXE has sha256 $sha..., build $BUILD needs $GH_SHA... (SPORE_GHIDRA_ANY_EXE=1 for a dump of the same code)" >&2
    exit 1
fi

NEW="$GH_PROJ.new"
rm -rf "$NEW"; mkdir -p "$NEW"
t0=$(date +%s)
# -import reads the exe only; the copy in the project is Ghidra's own database.
gh_ah "$NEW" spore -import "$EXE" > "$LOG" 2>&1
t1=$(date +%s)
gh_ah "$NEW" spore -process SporeApp.exe -noanalysis -scriptPath "$GH_HERE" \
    -postScript SporeSetup.java "${SYMS[@]}" >> "$LOG" 2>&1
t2=$(date +%s)
grep -E 'SporeSetup.java> ' "$LOG" | sed 's/.*SporeSetup.java> //'
echo "analysis $((t1 - t0)) s, setup $((t2 - t1)) s"

exec 9>"$GH_PROJ.lock"; flock -x 9
rm -rf "$GH_PROJ.old"
[[ -d "$GH_PROJ" ]] && mv "$GH_PROJ" "$GH_PROJ.old"
mv "$NEW" "$GH_PROJ"
rm -rf "$GH_PROJ.old"
echo "project: $GH_PROJ"
