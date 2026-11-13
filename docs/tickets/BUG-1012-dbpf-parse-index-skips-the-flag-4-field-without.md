---
id: BUG-1012
title: dbpf: parse_index skips the flag 4 field without a bounds check (a pointer past the index end on a short index)
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-8
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-net
commits: 226988e3
tests: unit (make test: net_tests test_dbpf_flag4)
signature:
docs: docs/04-netcode.md#netcode-n2-join-input, docs/06-testing.md#testing-n2-net
related:
switch:
---

## Report
Audit 2026-11-12 for 0.7.14, finding NET-8. Not seen at runtime, found by code reading (2026-11-13, plan/0.7.14).
A DBPF index of 4..7 bytes with flag 4 moves the read pointer past the end of the index (undefined behaviour in C++).
No read happens there: with count 0 the parse returns true with no entries, with count > 0 the per-entry bounds check
fails. No effect seen or expected; the index comes from the player's own EditorSaves / received creation packages.

## Root cause
Observed in code (main efd2e0ba), confidence high: mods/coop/net/dbpf.cpp:55 `if (flags & 4) p += 4;` without
`p + 4 > end`, unlike :53-54.

## Fix idea
`if (flags & 4) { if (p + 4 > end) return false; p += 4; }` (no switch: a bounds check of the BUG-0208 kind, under
net.dbbf_bounds).

## How to test
tests/net_tests.cpp (the dbpf tests next to BUG-0208's): an index of 4 bytes with flags = 4, count 1 and count 0:
the fix returns false for both; on main count 0 returns true (FAIL). tests/fuzz_decoders.cpp already fuzzes the parser
(an ASan / UBSan build would show the pointer overflow).

## Fix
`parse_index` checks that the flag-4 field lies inside the index before it skips it (`p + 4 > end` returns false), under the existing bounds switch `dbpf::g_bounds_fix` (net.dbbf_bounds, unit only by rule R6). Owner decision 7 allowed closing it with a guard test; the bounds check is one line of the BUG-0208 kind, so it is fixed, and its test fails on the old code.

## Verification
* Unit (make test): net_tests `test_dbpf_flag4` (a 4-byte index with flags = 4, count 0 and count 1: both refused). FAIL with `net_tests --nofix --only test_dbpf_flag4` (the test turns g_bounds_fix off under --nofix: count 0 taken; out/n2-unit/net_nofix_dbpf_flag4.txt in the fix/w49-net worktree), PASS in make test twice (out/n2-unit/net_pass_1.txt, net_pass_2.txt). The old side moves the pointer only (count 0 reads nothing), so it is safe in the ASan build.

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
- 2026-11-13: fixed on fix/w49-net (0.7.14 N2): the flag-4 skip is bounds-checked; FAIL/PASS unit runs.
