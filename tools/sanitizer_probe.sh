#!/usr/bin/env bash
# make test's sanitizer probe (BUG-0252): can $1 (HOSTCXX) build and run AddressSanitizer/UBSan and
# ThreadSanitizer programs? Writes the stamp $3 ("tsan=plain" or "tsan=setarch") or explains and fails.
# Usage: sanitizer_probe.sh <HOSTCXX> <dir> <stamp>
set -uo pipefail
cxx=$1 dir=$2 stamp=$3
mkdir -p "$dir"
src="$dir/sanitizer-probe.cpp"
echo 'int main() { return 0; }' > "$src"
fail() {
    echo "make test: $cxx cannot build/run $1 programs."
    echo "  clang: install the compiler-rt runtimes (Ubuntu: libclang-rt-<N>-dev, Fedora: compiler-rt), or use HOSTCXX=g++"
    echo "  ThreadSanitizer \"unexpected memory mapping\": gcc < 14 / clang < 18 with vm.mmap_rnd_bits=32: sudo sysctl vm.mmap_rnd_bits=28"
    echo "  (a local machine may skip them on purpose: SKIP_SANITIZERS=1 make test; never in CI)"
    echo "--- the probe's output:"
    cat "$dir/sanitizer-probe.log"
    exit 1
}
if ! { $cxx -fsanitize=address,undefined -o "$dir/sanitizer-probe-asan" "$src" && "$dir/sanitizer-probe-asan"; } \
        > "$dir/sanitizer-probe.log" 2>&1; then
    fail "AddressSanitizer"
fi
$cxx -fsanitize=thread -o "$dir/sanitizer-probe-tsan" "$src" > "$dir/sanitizer-probe.log" 2>&1 || fail "ThreadSanitizer"
if "$dir/sanitizer-probe-tsan" >> "$dir/sanitizer-probe.log" 2>&1; then
    echo tsan=plain > "$stamp"
elif grep -q 'unexpected memory mapping' "$dir/sanitizer-probe.log" \
        && setarch "$(uname -m)" -R "$dir/sanitizer-probe-tsan" >> "$dir/sanitizer-probe.log" 2>&1; then
    echo "make test: ThreadSanitizer runs only without address randomisation here: release_tsan under setarch -R"
    echo tsan=setarch > "$stamp"
else
    fail "ThreadSanitizer"
fi
