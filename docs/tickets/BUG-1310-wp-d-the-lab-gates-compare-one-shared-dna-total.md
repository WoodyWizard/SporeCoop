---
id: BUG-1310
title: WP-D: the lab gates compare one shared DNA total while every player has its own DNA
status: open
kind: tooling
stage: Cell, Creature
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in:
branch: fix/w57-dna
commits:
tests: tools/tests/gates_selftest.py, tools/tests/tools_selftest.py, tests/net_tests.cpp (test_diag_own_dna), tools/tests/coop_dna_own.py (dna_own_gates_off)
signature:
docs: docs/06-testing.md#testing-gates-own-dna
related: BUG-1291, BUG-1311, BUG-0379
---

## Report
First lab run of 0.7.16 (2026-11-19): own DNA per player (BUG-1291, all.shared_dna off by default) makes the host's and
the guest's DNA differ on purpose, but the gates still compared one shared total:
- monitor `totals`: "shared totals differ between the host and guest in Creature ... dna: host 150, guest 140"
  (dna_own, dna_own_reload, dna_own_rejoin of 20261119-104136; editor_sync_guest of 20261119-110020;
  evo_screens_switch_lowfps of 20261119-102828);
- sync-check: "MISMATCH ... dna: host 150.0, guest 120.0" (the same tests and dna_spend, dna_pace of 20261119-110020);
- detector (report only): `Creature:state:dna` "DNA 40 on the host, 15 on the guest".

## Root cause
A lab gate that does not know the new behaviour: the sync-check's digest, the monitor's own trace lines and the
detector's coop.wstate all compared a guest's game DNA with the host's game DNA. Every number in the failures is what
the tests' spends predict (checked per test in the night's logs):
- dna_own: 150/150, the guest spends 30 (150/120), the host 20 (130/120), a gain of 20 each (150/140, 170/160): the
  monitor saw 150/140, the sync-check 150/120 and 170/160: exactly those steps; the wallet table agreed (`wallet 1
  'Bob': creature=120.0`).
- dna_own_reload: the guest 25, the host 40: 110/125 (the test's "the guest has 15 more"); dna_own_rejoin: the guest 25:
  150/125; dna_spend: the guest's AddEvolutionPoints -25 of 40: 40/15; editor_sync_guest: the guest's mouth (25) of
  40: 40/15; dna_pace: the guest's -10, then +40 each at dna_pace=0: 60/50.
- evo_screens_switch_lowfps: both enter Creature with 115; the host's editor spends 50 (65), the guest's wallet starts at
  115 (first sight while the host's editor ran) and its own editor later spends 50: 65/115 between the two editors is
  the own model. That run also showed BUG-1311 (a one-frame shared total at the host's editor exit).
No DNA bug behind these gates apart from BUG-1311.

## Fix
The host exposes its record of each guest's own wallet and the gates compare against it (docs/06-testing.md#testing-gates-own-dna):
- mod: `dna_own_expect` / `dna_own_expect_cell` (dna.cpp): the wallet of a guest that plays its own (first sight not
  yet: the host's DNA, what the wallet starts at); false for a guest on the shared pool (setting, creature.mode host,
  an older mod). Trace own line: `bar=` (every game, Creature) and `dna_own=<id>:<v>,...` (host). coop.wstate: head
  `id=`, host rows `st wallet id= dna= spent=`. Sync-check: the digest carries `bar` (kTotalBar 3); for an own guest
  the host compares its dna with the wallet (kTotalDnaOwn 4, "own dna", never on the wire) with the wallet's last
  seconds as the late window, and the bars; a shared guest as before (its bar not compared).
- monitor `totals`: a guest listed in the host's dna_own= is compared with that record and its bar= with the host's;
  others with the host's dna= (unchanged).
- detector (worlddiff.diff_state): `Creature:state:dna_own` / `Cell:state:dna_spent_own` against the wallet row; the
  bar stays compared by `goal_progress`; a guest without a row as before.
- Switch `diag.own_dna` (off: every guest against the host's DNA, the night's gates). The shared mode is not loosened:
  no row / field for a guest on the shared pool, the old comparison runs; the own mode is stricter than before (the
  bar is compared too).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened.
