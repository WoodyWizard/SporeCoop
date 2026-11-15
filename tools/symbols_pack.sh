#!/usr/bin/env bash
# The debug symbols of a release (B5, BUG-1123): the unstripped DLLs of the build, published by the
# release job as SporeCoop-<ver>-debug-symbols.tar.xz next to the zip (the zip, the pack and the setup
# keep their stripped DLLs). tools/crash_triage.py downloads it to name the functions and source lines
# of `coop.dll+0x...` / `sporekit.dll+0x...` frames in a player's report of that version.
#
#   tools/symbols_pack.sh <build dir> <version> <out dir> [<stage dir of tools/package.sh>]
#
# Writes <out dir>/SporeCoop-<ver>-debug-symbols.tar.xz with one folder SporeCoop-<ver>-debug-symbols/:
# coop.dll, sporekit.dll, steam_api.sporekit.dll (the loader under its shipped name) and SYMBOLS.txt
# (version, commit, sha256 and tools/pe_version.py --repro-sha256 of each file, and of the shipped
# stripped copy when the stage dir is given). Refuses DLLs whose VERSIONINFO is not <version>.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:?usage: symbols_pack.sh <build dir> <version> <out dir> [<stage dir>]}"
ver="${2:?version}" out="${3:?out dir}" stage="${4:-}"
name="SporeCoop-$ver-debug-symbols"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/$name" "$out"
declare -A shipped=([coop.dll]=SporeCoop/mods/coop.dll [sporekit.dll]=SporeCoop/sporekit.dll
                    [steam_api.sporekit.dll]=SporeCoop/steam_api.sporekit.dll)
cp "$build/coop.dll" "$tmp/$name/coop.dll"
cp "$build/sporekit.dll" "$tmp/$name/sporekit.dll"
cp "$build/steam_api.dll" "$tmp/$name/steam_api.sporekit.dll"
python3 "$ROOT/tools/pe_version.py" --expect "$ver" "$tmp/$name"/*.dll >/dev/null \
    || { python3 "$ROOT/tools/pe_version.py" --expect "$ver" "$tmp/$name"/*.dll >&2; echo "symbols: the DLLs in $build are not $ver" >&2; exit 1; }
commit="${SPORECOOP_COMMIT:-${GITHUB_SHA:-$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)}}"
{
    echo "SporeCoop $ver debug symbols (unstripped DLLs with DWARF; image base and code are the shipped ones)"
    echo "commit $commit"
    echo "use: tools/crash_triage.py <report> (downloads this file by the report's version), or"
    echo "     llvm-symbolizer --obj=coop.dll --relative-address 0x<offset of coop.dll+0x...>"
    for f in coop.dll sporekit.dll steam_api.sporekit.dll; do
        echo "file $f sha256 $(sha256sum "$tmp/$name/$f" | cut -d' ' -f1)"
        if [[ -n "$stage" && -f "$stage/${shipped[$f]}" ]]; then
            echo "shipped $f sha256 $(sha256sum "$stage/${shipped[$f]}" | cut -d' ' -f1)" \
                 "repro-sha256 $(python3 "$ROOT/tools/pe_version.py" --repro-sha256 "$stage/${shipped[$f]}" | cut -d' ' -f1)"
        fi
    done
} > "$tmp/$name/SYMBOLS.txt"
mtime="${SOURCE_DATE_EPOCH:-$(git -C "$ROOT" log -1 --format=%ct 2>/dev/null || echo 0)}"
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$mtime" -C "$tmp" -cf - "$name" \
    | xz -T0 -6 > "$out/$name.tar.xz.tmp"
mv "$out/$name.tar.xz.tmp" "$out/$name.tar.xz"
echo "$out/$name.tar.xz"
