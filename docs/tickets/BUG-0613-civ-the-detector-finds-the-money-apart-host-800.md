---
id: BUG-0613
title: Civ: the detector finds the money apart (host -800, guest 0) in civ_coop
status: open
kind: desync
stage: Civ
reported: 2026-11-06
source: detector, C2 regression run (fix/w45-cell out/lab/20261106-015141, civ_coop)
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
The divergence detector (SPORE_DETECTOR=20) reported a row `Civilization:state:money` with host -800 and guest 0 in civ_coop (smoke) on fix/w45-cell (out/lab/20261106-015141 in that worktree, civ_coop.log). Not analysed: whether the money of the guest's civ should mirror the host's, or the row is an artifact of the detector. For 0.7.10 (V2).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
