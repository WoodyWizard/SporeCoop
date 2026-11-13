---
id: BUG-1012
title: dbpf: parse_index skips the flag 4 field without a bounds check (a pointer past the index end on a short index)
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-8
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
