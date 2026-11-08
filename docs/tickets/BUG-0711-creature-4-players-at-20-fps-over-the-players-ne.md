---
id: BUG-0711
title: Creature, 4 players at 20 fps over the players' network: a guest draws a partner 16.5 m off its own position after a 1.2 s state gap (monitor drawn)
status: open
kind: test
stage: Creature
reported: 2026-11-08
source: fix/w46-k7-findings lab 20261108-012301 multi4_creature_slow
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature: MONITOR-VIOLATION drawn guest: guest draws extra1 16.5 m from where extra1's own game has it (limit 5.2 m, Creature) on 2 lines from s 170
docs:
related: BUG-0674, BUG-0675, BUG-0170
---

## Report
multi4_creature_slow (SPORE_FPS=20 SPORE_NET=players, seed 1035603402) on fix/w46-k7-findings a1d1cc7a, lane 10, lab
20261108-012301: the only failure was the monitor gate `drawn guest: guest draws extra1 16.5 m ... (limit 5.2 m)` on
2 lines from s 170. The guest's partner line for Carol at s 170.052: `state_age=1.2 s rtt=142 ms drawn_v=2.6`, flags 0x3
(moving); at s 180 state_age 0.0. Inferred: a latency spike of the players' network (spike_every 60 s) left the
guest's copy 1.2 s without a state: the K4 class (BUG-0674 / BUG-0675, a body extrapolated or held through a spike).
No fault, no freeze. Reproduce: `tools/lab.py run multi4_creature_slow` (seen once).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
