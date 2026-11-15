# Shared settings of tools/ghidra/q.sh and tools/ghidra/mkproject.sh (sourced, not run).
GH_HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GH_ROOT="$(cd "$GH_HERE/../.." && pwd)"
# The main checkout (projects live in its out/, shared by every worktree).
GH_MAIN="$(dirname "$(git -C "$GH_ROOT" rev-parse --path-format=absolute --git-common-dir)")"
GH_GHIDRA="${GHIDRA_HOME:-$HOME/.local/opt/ghidra_12.1.4_PUBLIC}"
if [[ -z "${JAVA_HOME:-}" && -d "$HOME/.local/opt/jdk-21.0.12.1+1" ]]; then
    export JAVA_HOME="$HOME/.local/opt/jdk-21.0.12.1+1"
fi
[[ -n "${JAVA_HOME:-}" ]] && export PATH="$JAVA_HOME/bin:$PATH"
export GHIDRA_HEADLESS_MAXMEM="${GHIDRA_HEADLESS_MAXMEM:-8G}"
GH_AH="$GH_GHIDRA/support/analyzeHeadless"

# gh_build <2024>: sets GH_PROJ (project dir), GH_SHA (exe sha256 prefix), GH_EXE (default exe). GOG 2016 is not
# supported and has no symbol files since 0.7.15 (BUG-1125); its old project (out/ghidra-full) is not used.
gh_build() {
    case "$1" in
        2024) GH_PROJ="$GH_MAIN/out/ghidra-2024"; GH_SHA=dc04aee5
              GH_EXE="$(dirname "$GH_MAIN")/SporebinEP1/SporeApp.exe" ;;
        2016) echo "GOG 2016 is not supported (no symbol files since 0.7.15, BUG-1125): use --build 2024" >&2; return 1 ;;
        *) echo "unknown build '$1' (2024)" >&2; return 1 ;;
    esac
    GH_PROJ="${SPORE_GHIDRA_PROJ:-$GH_PROJ}"
}
