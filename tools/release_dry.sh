#!/usr/bin/env bash
# make release-dry (A2, BUG-1120): the release build of a commit as GitHub Actions makes it, run locally
# before a tag, so a tag never fails only in CI (v0.7.3.4, v0.7.11 and v0.7.14 did: other headers, a
# Python 3.12 f-string, a private ticket id in the notes).
#
#   tools/release_dry.sh [--rev <commit>] [--keep] [--jobs N]
#
# * a `git archive` copy of the commit (default HEAD; uncommitted changes are NOT in it) in an empty
#   folder, made a shallow (depth 1) repository of that commit like CI's checkout: no history (the
#   git checks of tickets.py are skipped as in CI), no out/ (no private tickets, no lab results, no old
#   dist), no untracked files, no other worktrees;
# * CI's Python (CI_PYTHON of tools/toolchain.env, `uv run --no-project --python 3.10`) and Pillow,
#   first on PATH as python3; the pinned llvm-mingw (its folder must carry LLVM_MINGW_VERSION); a clean
#   environment (env -i: PATH, HOME, USER, LANG=C.UTF-8, CI=true as GitHub sets it, no MAKEFLAGS);
# * every step of .github/workflows/build.yml that can run here, in its order: the Python check,
#   release_check.sh (tag = v<COOP_VERSION>, notes = its CHANGELOG section, tickets.py notes-check),
#   make, make test, package.sh, then the release job's checks: every asset named by the publish
#   step exists, SHA256SUMS.txt over them, the zip unpacks and holds the binaries the attestation names.
# Not covered: CI's host compiler for `make test` (ubuntu-22.04's clang 14 and libstdc++ 12; here the
# local clang++), the `gh release create` upload itself and the attestation.
# Exit 0 only when every step passed; the folder is removed then (kept on failure or with --keep).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rev=HEAD keep=0 jobs="${RELEASE_DRY_JOBS:-8}"
while (($#)); do
    case "$1" in
        --rev) rev="${2:?--rev needs a commit}"; shift 2 ;;
        --keep) keep=1; shift ;;
        --jobs) jobs="${2:?--jobs needs a number}"; shift 2 ;;
        -h|--help) sed -n '2,25p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "release-dry: unknown argument $1" >&2; exit 2 ;;
    esac
done
commit="$(git -C "$ROOT" rev-parse --verify "$rev^{commit}")"
if [[ "$rev" == HEAD ]] && [[ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]]; then
    echo "release-dry: WARNING: uncommitted changes in $ROOT are not part of the check (it builds $commit)" >&2
fi
# the pins of that commit (not of the working tree)
envfile="$(git -C "$ROOT" show "$commit:tools/toolchain.env")"
pin() { sed -n "s/^$1=//p" <<<"$envfile" | head -1; }
CI_PYTHON="$(pin CI_PYTHON)" PILLOW_VERSION="$(pin PILLOW_VERSION)" LLVM_MINGW_VERSION="$(pin LLVM_MINGW_VERSION)"
[[ -n "$CI_PYTHON" && -n "$PILLOW_VERSION" && -n "$LLVM_MINGW_VERSION" ]] \
    || { echo "release-dry: tools/toolchain.env of $commit lacks CI_PYTHON / PILLOW_VERSION / LLVM_MINGW_VERSION" >&2; exit 2; }
LLVM_MINGW="${LLVM_MINGW:-$HOME/.local/opt/llvm-mingw-$LLVM_MINGW_VERSION-ucrt-ubuntu-22.04-x86_64}"
case "$(basename "$LLVM_MINGW")" in
    llvm-mingw-"$LLVM_MINGW_VERSION"-*) ;;
    *) echo "release-dry: $LLVM_MINGW is not llvm-mingw $LLVM_MINGW_VERSION (tools/toolchain.env)" >&2; exit 2 ;;
esac
[[ -x "$LLVM_MINGW/bin/i686-w64-mingw32-clang++" ]] || { echo "release-dry: no toolchain in $LLVM_MINGW" >&2; exit 2; }
command -v uv >/dev/null || { echo "release-dry: uv is needed (CI's Python $CI_PYTHON)" >&2; exit 2; }

dir="${RELEASE_DRY_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/sporecoop-release-dry.XXXXXX")}"
mkdir -p "$dir/src"
[[ -z "$(ls -A "$dir/src")" ]] || { echo "release-dry: $dir/src is not empty" >&2; exit 2; }
echo "release-dry: $commit ($(git -C "$ROOT" log -1 --format=%s "$commit")) in $dir/src"
git -C "$ROOT" archive --format=tar "$commit" | tar -x -C "$dir/src"
# CI checks the repository out with fetch-depth 1: the same commit, shallow, no out/. The files stay
# the archive's (reset moves only HEAD and the index), so `git status` there is clean.
git -C "$dir/src" init -q
git -C "$dir/src" fetch -q --depth 1 "file://$ROOT" "$commit"
git -C "$dir/src" reset -q FETCH_HEAD
[[ -z "$(git -C "$dir/src" status --porcelain)" ]] || { echo "release-dry: the archive differs from the commit" >&2; exit 2; }

start=$SECONDS
set +e
env -i PATH="$PATH" HOME="$HOME" USER="${USER:-$(id -un)}" LANG=C.UTF-8 TERM=dumb CI=true \
    TMPDIR="${TMPDIR:-/tmp}" SPORECOOP_COMMIT="$commit" \
    uv run --no-project --python "$CI_PYTHON" --with "pillow==$PILLOW_VERSION" -- \
    bash -euo pipefail -c '
cd "$1"; LLVM_MINGW="$2"; jobs="$3"; want_py="$4"
step() { printf "\n== release-dry: %s\n" "$*"; }
# The Python of uv is relocatable: started under another argv[0] (tools_selftest.py fakes a Wine SporeApp.exe
# with executable=sys.executable) it cannot find its library; /usr/bin/python3 of the runner can.
# PYTHONHOME gives it back (python3 -I ignores it, and finds it from pyvenv.cfg anyway).
export PYTHONHOME="$(python3 -c "import sys; print(sys.base_prefix)")"
step "python3 is CI_PYTHON $want_py"
python3 -c "import sys; v = \"%d.%d\" % sys.version_info[:2]; print(\"python3\", sys.version.split()[0]); sys.exit(0 if v == \"$want_py\" else 1)"
ver="$(sed -n "s/^#define COOP_VERSION \"\(.*\)\"/\1/p" mods/coop/coop.cpp | head -1)"
step "release_check.sh v$ver (the tag build: tag = COOP_VERSION, notes, notes-check without out/tickets)"
tools/release_check.sh "v$ver" notes.md
step "make -j$jobs (loader, SporeKit, coop, resources)"
make -j"$jobs" LLVM_MINGW="$LLVM_MINGW"
step "make test HOSTCXX=clang++"
make test HOSTCXX=clang++
step "tools/package.sh"
LLVM_MINGW="$LLVM_MINGW" tools/package.sh
step "the release job: assets, SHA256SUMS.txt, the zip"
mkdir -p dist && cp out/dist/SporeCoop-*.zip out/dist/SporeCoop-*.pack out/dist/SporeCoopSetup.exe dist/
assets=("SporeCoop-$ver.zip" "SporeCoop-$ver.pack" SporeCoopSetup.exe)
for s in out/dist/SporeCoop-*-debug-symbols.tar.xz; do  # B5 (BUG-1123), once package.sh writes it
    if [[ -e "$s" ]]; then cp "$s" dist/; assets+=("$(basename "$s")"); fi
done
if grep -q "debug-symbols" .github/workflows/build.yml && [[ ! -s "dist/SporeCoop-$ver-debug-symbols.tar.xz" ]]; then
    echo "release-dry: the workflow publishes SporeCoop-$ver-debug-symbols.tar.xz but package.sh did not write it" >&2; exit 1
fi
for a in "${assets[@]}"; do [[ -s "dist/$a" ]] || { echo "release-dry: asset dist/$a missing" >&2; exit 1; }; done
tools/release_check.sh "v$ver" notes.md
(cd dist && sha256sum "${assets[@]}" > SHA256SUMS.txt && cat SHA256SUMS.txt)
mkdir -p inzip && unzip -q "dist/SporeCoop-$ver.zip" -d inzip
for b in inzip/SporeCoop/steam_api.sporekit.dll inzip/SporeCoop/sporekit.dll inzip/SporeCoop/mods/coop.dll; do
    [[ -s "$b" ]] || { echo "release-dry: $b missing in the zip (the attestation names it)" >&2; exit 1; }
done
ls -la dist
' release-dry "$dir/src" "$LLVM_MINGW" "$jobs" "$CI_PYTHON" 2>&1 | tee "$dir/release-dry.log"
rc=${PIPESTATUS[0]}
set -e
if ((rc == 0)); then
    echo "release-dry: PASS $commit in $((SECONDS - start)) s (Python $CI_PYTHON, llvm-mingw $LLVM_MINGW_VERSION)"
    if ((keep)); then echo "release-dry: kept $dir"; else rm -rf "$dir"; fi
else
    echo "release-dry: FAIL $commit (exit $rc); the copy and release-dry.log stay in $dir" >&2
fi
exit "$rc"
