---
id: BUG-1092
title: Creature: puppet_air fails on rel/0.7.14: the host's puppet of the walking guest jumps 2-8 times (no jump check)
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
fix/w49-raid2 regression (2026-11-13): `puppet_air` FAILs on every run with "host watches guest: no jump of the puppet
while it walks (N)", N = 1-8 (the puppet's position steps while the guest's creature walks); the other checks pass
(jumps shown, walk gait, falls). Also on the base rel/0.7.14 at 470f1ccd built alone (lab 20261113-185616: 7 and 4),
so not fix/w49-raid2's change; on fix/w49-raid2 with and without `game.air_culled` the same (20261113-185217: 2, 7;
20261113-185217-2 off: 3, 8; 20261113-184358: 1). Last FAIL of this kind before: fix/w41-puppet-look (2026-11-04).
Reproduce: `lab.py run puppet_air --repeat 2`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
