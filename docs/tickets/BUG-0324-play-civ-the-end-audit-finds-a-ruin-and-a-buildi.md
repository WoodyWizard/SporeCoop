---
id: BUG-0324
title: play_civ: the end audit finds a ruin (and a building) on the guest that the host no longer has
status: open
kind: desync
stage: Civ
reported: 2026-10-30
source: play_civ.py --seed 11 / --seed 9, fix/w13-tribe-civ and fix/w14-tribe-civ
role: guest
versions: main 4dbacc5
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
`play_civ.py --minutes 10` on lane 7 (2026-10-30): the end audit (the only one that reports ruins
and building counts as problems, once, without a second look) failed with
* seed 11 (fix/w14-tribe-civ, out/w14/play_civ_11.log of the worktree): "city 1: 0 ruins on the host,
  1 on the guest", "city 1: 1 buildings on the host, 2 on the guest", "city 3: 0 ruins on the host, 1
  on the guest";
* seed 9 (fix/w13-tribe-civ, out/w13/play_civ9_a.log): "city 2: 0 ruins on the host, 1 on the guest".
During the session the audits count such differences (`ruins_differ`, `buildings_differ`) without
failing; the guest's `buildings_fixed=+N/-M` shows the reconciliation correcting buildings.

## Root cause
Not analysed. Candidates (inferred): a capture removes the ruins on the host (docs/re/civ.md 9.4)
and the guest's reconciliation of that city comes a few seconds later (the end audit has no
second look, unlike owners since BUG-0206); or a real desync of ruins after a capture.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
