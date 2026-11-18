# Shared settings of tools/ghidra/q.sh and tools/ghidra/mkproject.sh (sourced, not run).
GH_HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GH_ROOT="$(cd "$GH_HERE/../.." && pwd)"
# The main checkout (projects live in its out/, shared by every worktree).
GH_MAIN="$(dirname "$(git -C "$GH_ROOT" rev-parse --path-format=absolute --git-common-dir)")"
GH_GHIDRA="${GHIDRA_HOME:-$HOME/.local/opt/ghidra_12.1.4_PUBLIC}"
if [[ -z "${JAVA_HOME:-}" ]]; then
    for j in "$HOME/.local/opt/jdk-21.0.12.1+1" "$HOME"/.local/opt/jdk-21*; do
        [[ -d "$j" ]] && { export JAVA_HOME="$j"; break; }
    done
fi
[[ -n "${JAVA_HOME:-}" ]] && export PATH="$JAVA_HOME/bin:$PATH"
export GHIDRA_HEADLESS_MAXMEM="${GHIDRA_HEADLESS_MAXMEM:-8G}"
GH_AH="$GH_GHIDRA/support/analyzeHeadless"

# Windows (Git Bash, 2026-11-17): Ghidra's .bat launcher with Windows paths, no flock (one user, runs in turn)
GH_WIN=0
[[ "${OSTYPE:-}" == msys* || "${OSTYPE:-}" == cygwin* ]] && GH_WIN=1
if [[ $GH_WIN == 1 ]]; then
    [[ -n "${JAVA_HOME:-}" ]] && export JAVA_HOME="$(cygpath -w "$JAVA_HOME")"
    command -v flock > /dev/null || flock() { :; }
fi
# gh_ah <args...>: analyzeHeadless; on Windows every argument that is an existing path (or under one) goes as a
# Windows path
gh_ah() {
    if [[ $GH_WIN != 1 ]]; then "$GH_AH" "$@"; return; fi
    local a out=()
    for a in "$@"; do
        if [[ "$a" == /* ]]; then out+=("$(cygpath -w "$a")"); else out+=("$a"); fi
    done
    "$GH_AH.bat" "${out[@]}"
}

# gh_build <2016|2024>: sets GH_PROJ (project dir), GH_SHA (exe sha256 prefix), GH_EXE (default exe)
gh_build() {
    case "$1" in
        2016) GH_PROJ="$GH_MAIN/out/ghidra-full"; GH_SHA=922af7c7
              GH_EXE="$(dirname "$GH_MAIN")/../_ref/gog2016/SporeApp.exe" ;;
        2024) GH_PROJ="$GH_MAIN/out/ghidra-2024"; GH_SHA=dc04aee5
              GH_EXE="$(dirname "$GH_MAIN")/SporebinEP1/SporeApp.exe" ;;
        *) echo "unknown build '$1' (2016 or 2024)" >&2; return 1 ;;
    esac
    GH_PROJ="${SPORE_GHIDRA_PROJ:-$GH_PROJ}"
}
