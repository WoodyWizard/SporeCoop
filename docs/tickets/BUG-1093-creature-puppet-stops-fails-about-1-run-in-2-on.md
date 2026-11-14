---
id: BUG-1093
title: Creature: puppet_stops fails about 1 run in 2 on rel/0.7.14: the puppet goes 0.5-0.7 m past 1-2 of 8-17 stops (coop.bodyrec)
status: open
kind: desync
stage: Creature
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Seen on fix/w49-puppet (BUG-1092) regression runs, with and without that branch's switch: `puppet_stops` FAILs "the
puppet does not go past where <walker>'s own creature stopped (over 0.3 m after 1-2 of 8-17 stops, coop.bodyrec)",
max 0.48-0.66 m. Fix on: FAIL 20261113-193854 (lane 3), 20261113-195009 (lane 2); PASS 20261113-191413,
20261113-195639. `SPORE_LAB_FIXOFF="puppet_stops=drive.agl_lerp"`: FAIL 20261113-200403 #1 (lane 2), PASS
20261113-195007, 20261113-200400 #1 (lane 3). drive.agl_lerp only changes the radial height of a PF_JUMPING state,
so not that change. Not checked on v0.7.13. Reproduce: `lab.py run puppet_stops --repeat 4 --retry 0`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
