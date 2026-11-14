---
id: BUG-1090
title: Tribe: tribe_editor_world fails on rel/0.7.14: the co-op epic is still alive in the host's game after the editor's share save
status: open
kind: bug
stage: Tribe
reported: 2026-11-13
source: lab 20261113-183059 tribe_editor_world
role: host
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests: tribe_editor_world
signature:
docs:
related: BUG-0217, BUG-0121
---

## Report
Found by fix/w49-bundle in its regression runs (lane 6 / 7, rel/0.7.14 f7480eae + BUG-1084): `tribe_editor_world` failed 3
times out of 3 on the same check, "FAIL the host's game has no co-op epic left: it left before the share save (BUG-0121
guard ran)": after the host's editor visit and the share ("the guest loaded the shared world after 150 s") the host
still had `live_epics=2 tracked=<epic>` (lab 20261113-183059, 20261113-184244; and 20261113-184241 with
`tribe.away_tree_end` off, so not BUG-1084's change, which runs only on guests). The epic is still tracked through the
editor (BUG-0217's checks pass); only the "left before the share save" step fails. Last PASS seen: fix/w5-world
20261029-003648. No faults in either game.
Reproduce: `lab.py run tribe_editor_world --lane N`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
