#!/usr/bin/env bash
# The release job's checks (.github/workflows/build.yml, BUG-0097), runnable locally before a tag:
#   tools/release_check.sh vX.Y.Z [notes.md]
# Fails when the tag is not COOP_VERSION (mods/coop/coop.cpp): the in-game updater takes the version
# from the tag and the pack from COOP_VERSION, a mismatch offers an update that never applies.
# Writes the release notes: exactly the "## [X.Y.Z]" section of CHANGELOG.md (not the X.Y.Z.N ones
# after it); fails when there is none.
set -euo pipefail
tag="${1:?usage: release_check.sh vX.Y.Z [notes.md]}"
out="${2:-notes.md}"
ver="${tag#v}"
code=$(sed -n 's/^#define COOP_VERSION "\(.*\)"/\1/p' mods/coop/coop.cpp | head -1)
if [[ "$ver" != "$code" ]]; then
    echo "release: tag $tag but COOP_VERSION is \"$code\" (bump mods/coop/coop.cpp, or tag v$code)" >&2
    exit 1
fi
# index(...) == 1: the literal heading "## [X.Y.Z]" (dots are no wildcards, "]" is required)
awk -v h="## [$ver]" 'index($0, h) == 1 {on = 1; next} /^## / && on {exit} on' CHANGELOG.md \
    | sed -e '/./,$!d' | sed -e ':a' -e '/^\n*$/{$d;N;ba' -e '}' > "$out"
if [[ ! -s "$out" ]]; then
    echo "release: CHANGELOG.md has no section \"## [$ver]\" (or it is empty)" >&2
    exit 1
fi
# the notes name under "### Fixed" only fixed tickets (BUG-0216)
python3 "$(dirname "${BASH_SOURCE[0]}")/tickets.py" notes-check "$ver" "$out"
echo "release $tag: COOP_VERSION matches, notes $(wc -l < "$out") lines in $out"
