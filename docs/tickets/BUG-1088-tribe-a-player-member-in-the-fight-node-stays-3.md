---
id: BUG-1088
title: Tribe: a player member in the fight node stays 3-8.75 m over the ground for 36 s on every game (monitor ground gate fails the full campaign)
status: open
kind: bug
stage: Tribe
reported: 2026-11-13
source: lab
role: host
versions: 
fixed_in:
branch:
commits:
tests: campaign (lab gate monitor ground)
signature:
docs:
related: BUG-0945
---

## Report
fix/w49-camp, the full campaign lab 20261113-142203 (lane 4): the lab failed the run on the monitor's ground gate
(besides the Tribe timeout): "the tribe members 6.15 m over the ground (Tribe) on 2 lines from s 1630 (the members of
partner 2, one of them up for 19 s without a break)" on all four games. The host's log names it: member #16 of player
2 (Carol), `bt=05218BB3` (the fight node), hp 90, speed 6.3, 3.14 m up at s 1620.8 (14:49:26), "down again after
35.9 s over 3 m, highest 8.75 m" (14:50:02); Carol's game shows the same member up (agl_hi 4.87, 6.15, 6.50,
agl_held 9.3-29.4 s). This was the Tribe play phase (hunts), not a cinematic. BUG-0945's toss by a blow lasts 1.3-1.5 s;
this one 36 s. Seen once in this branch's runs.
Reproduce: the full campaign (`lab.py run =campaign`), look for "tribe: member #.. is .. m over the ground" with a
long "down again after".

## Root cause
Not analysed (outside fix/w49-camp's package). Inferred: a member thrown in a fight landed on something the terrain
height does not include (a rock, a creature's body) or kept its upward toss; the reading is the host's own game.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
