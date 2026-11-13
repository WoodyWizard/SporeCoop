---
id: BUG-1072
title: Tribe: the host's chief in the same main animation at the same moment on both games still stands another way on the guest (bent vs upright, the staff at another angle)
status: open
kind: desync
stage: Tribe
reported: 2026-11-13
source: fix/w49-tribe3 final check, lab 20261113-070558 #3 view 4, 20261113-063317 #3 view 1
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1069, BUG-0988
---

## Report
After BUG-1069's fix the stills of same_view_tribe show the host's chief in the same main animation at the same
moment on both games (the test prints both games' rows at each still), but in two stills its body still differs:
* lab 20261113-070558 #3 view 4: both games `anim=02481DE5` (the idle, a loop) t 7.24 / 7.21, hold 0: on the host the
  chief stands bent with the staff low to the left, on the guest upright with the staff held out.
* lab 20261113-063317 #3 view 1: both `anim=04C6CB02` t 1.43 / 1.46, hold 0: the staff slanted on the host, upright
  on the guest.
Not analysed. Candidates: the other playing tracks of the animation state (only the main track, +0xF0C[+0xFA4], is
relayed and timed: docs/re/tribe.md#tribe-anim-queued-start lists up to 16 playing tracks), their blend weights, or a
look-at / IK target the mirror does not share.
Reproduce: `lab.py run same_view_tribe --repeat 3`, compare the chief at each still with the `host's chief at the
still:` lines; dump every playing track (id, time, weight) of the chief on both games at that moment.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
