---
id: BUG-1095
title: Creature: a puppet in the air stalls when the partner's states come late and jumps 1 m when they come (drive.puppet_fall in the air)
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
related: BUG-1093, BUG-0680, BUG-0376
---

## Report
Seen on fix/w49-stops (BUG-1093) in `puppet_stops` (lab 20261113-203423 #4, lane 2, host sees guest): "no jump of the
puppet while it walks (1)", a 1.02 m step of the drawn puppet in one 16 ms frame at 22.82 s. Observed in the track rows
(out/lane2/track/puppet_walk_host_sees_guest_20261113-204611.json, columns ex / age): the guest's creature was in the air
(flags 15, PF_JUMPING, 1.8-2.5 m over the host's ground, 9-10 m/s); its states came 0.2 s apart (age 0.203); past the
newest state the target's speed fell with drive.puppet_fall (rv 9.70 -> 1.83 m/s at ex 0.02 -> 0.10 s), the puppet,
placed on the target in the air, stopped in mid-air; the next state put the target 0.86 m on and the puppet was placed
there at once. Inferred: BUG-0680's fall of the extrapolation (a partner that stopped in a spike) also applies to an
airborne state, which cannot stop in the air. Reproduce: `lab.py run puppet_stops --repeat 4` (~1 run in 8 on a
loaded lab).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
