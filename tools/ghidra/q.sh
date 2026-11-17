#!/bin/bash
# Batch queries against the private Ghidra project of one game build (docs/05-re-notes.md 5.2).
#
#   tools/ghidra/q.sh [--build 2016|2024] <queries-file|-> <output-file>
#
# Query syntax: see the header of tools/ghidra/Q.java (dec, asm, fasm, info, xref, callers,
# callees, calls, scan, bytes, str, dd, sym). Several q.sh runs may go at the same time: each one
# queries its own reflink snapshot of the project (out/ghidra-runs/), so Ghidra's project lock and
# a concurrent tools/ghidra/mkproject.sh do not get in the way. Nothing is saved back.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BUILD=2016
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build) BUILD="$2"; shift 2 ;;
        --build=*) BUILD="${1#*=}"; shift ;;
        -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
        *) break ;;
    esac
done
if [[ $# -ne 2 ]]; then sed -n '2,10p' "$0" >&2; exit 2; fi
gh_build "$BUILD"
QUERIES="$1"; OUTPUT="$(realpath -m "$2")"
[[ -f "$GH_PROJ/spore.gpr" ]] || { echo "no project $GH_PROJ: run tools/ghidra/mkproject.sh --build $BUILD" >&2; exit 1; }

RUNS="$GH_MAIN/out/ghidra-runs"
mkdir -p "$RUNS"
# snapshots left behind by killed runs
find "$RUNS" -mindepth 1 -maxdepth 1 -mmin +720 -exec rm -rf {} + 2>/dev/null || true
RUN="$(mktemp -d "$RUNS/$BUILD-XXXXXX")"
trap 'rm -rf "$RUN"' EXIT
trap 'exit 130' INT TERM
if [[ "$QUERIES" == - ]]; then cat > "$RUN/queries.txt"; else cp "$QUERIES" "$RUN/queries.txt"; fi

# Snapshot under a shared lock (mkproject.sh takes it exclusively while it writes the project).
# On btrfs/xfs the reflink copy takes well under a second; elsewhere it is a plain copy.
exec 9>"$GH_PROJ.lock"
flock -s 9
cp -a --reflink=auto "$GH_PROJ/spore.gpr" "$GH_PROJ/spore.rep" "$RUN/"
flock -u 9
exec 9>&-

rm -f "$OUTPUT"
t0=$(date +%s%N)
if ! gh_ah "$RUN" spore -process SporeApp.exe -noanalysis -readOnly \
        -scriptPath "$GH_HERE" -postScript Q.java "$RUN/queries.txt" "$OUTPUT" \
        > "$RUN/ghidra.log" 2>&1 || ! [[ -s "$OUTPUT" ]]; then
    grep -E 'ERROR|Exception|error:' "$RUN/ghidra.log" | head -40 >&2
    echo "q.sh: Ghidra failed (full log above, run kept in $RUN.log)" >&2
    cp "$RUN/ghidra.log" "$RUN.log"
    exit 1
fi
grep -E 'Q.java.*(ERROR|Exception)|SCRIPT ERROR' "$RUN/ghidra.log" >&2 || true
ms=$(( ($(date +%s%N) - t0) / 1000000 ))
printf 'q.sh: %s (build %s, %d.%01d s)\n' "$OUTPUT" "$BUILD" $((ms / 1000)) $((ms % 1000 / 100)) >&2
