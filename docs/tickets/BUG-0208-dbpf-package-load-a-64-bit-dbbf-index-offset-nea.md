---
id: BUG-0208
title: dbpf::Package::load: a 64-bit DBBF index offset near 2^64 wraps the bounds check and reads before the file buffer
status: fixed
kind: crash
stage: All
reported: 2026-10-28
source: fuzz_decoders (libFuzzer), fix/w4-census
role: both
versions: main 7874654 and earlier
fixed_in: 0.7.3.0
branch: fix/w4-census
commits:
tests: net_tests test_dbbf_wrap (make test; `net_tests_asan --nofix` fails with a heap-buffer-overflow), fuzz_decoders (make test; `fuzz_decoders --nofix` fails)
signature: heap-buffer-overflow / heap-use-after-free read in dbpf::parse_index (le32) from Package::load
docs: docs/04-netcode.md#netcode-fuzzing
related:
---

## Report
libFuzzer on tests/fuzz_decoders.cpp (`-DFUZZ_LIBFUZZER`, decoder 8 = `dbpf::Package::load` + `read`) found within ~30 s: a 157-byte package with the DBBF magic and an index offset of 0xFFFFFFFFFFFFFFFF read one byte before the file buffer.

## Root cause
Observed (ASan): `Package::load` checked `info.offset + info.size > n`. In a DBBF header the index offset is 64-bit (`le64(f + 0x38)`), so an offset near 2^64 plus the 32-bit size wraps to a small number and passes; `parse_index` then reads at `file + offset`, i.e. before the buffer. The per-entry check `e.offset + e.size <= n` wraps the same way (an entry offset near 2^64 is kept and `Package::read` would read far outside the file).

Who is affected (inferred from reading): `Package::load` reads only local files today (the player's EditorSaves.package, creations.cpp `load_editor_saves`); a damaged or hostile package file could crash the game. Packages from other players do not go through `Package::load` (Pollination reads use `index_info` + `read_at`, which reads through the file API and is bounded by it).

## Fix
Both checks are written without the sum: `offset > n || size > n - offset` and `e.offset <= n && e.size <= n - e.offset`. `dbpf::g_bounds_fix` (false: the old sum) is the twin switch for the unit tests (`net_tests --nofix`, `fuzz_decoders --nofix`).

## Verification
Unit test `test_dbbf_wrap` (tests/net_tests.cpp, in `make test`): with the fix the package is rejected and the wrapped entry dropped (ALL OK); `build/host/net_tests_asan --nofix` aborts with `heap-buffer-overflow ... le32 dbpf.cpp:9 <- parse_index <- Package::load <- test_dbbf_wrap`. The deterministic `fuzz_decoders` aims a header mutation at the 64-bit offset too: `fuzz_decoders --nofix` fails (ASan in parse_index), `fuzz_decoders` ALL OK.

## History
- 2026-10-28: opened.
- 2026-10-28 (fix/w4-census): found by the libFuzzer build of fuzz_decoders, fixed with the overflow-free checks, regression test in net_tests and in fuzz_decoders (both FAIL with `--nofix`).
- 2026-10-28: released in 0.7.3.0.
