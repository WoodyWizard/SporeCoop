---
id: BUG-1168
title: Civ: sync_mobile fails civ/vehicles 90% within 3.0 m (one vehicle, p90 4.2 m) on 0.7.14.1 and rel/0.7.15
status: duplicate
kind: desync
stage: Civ
reported: 2026-11-15
source: B1 impact run 20261115-133237
role: 
versions: v0.7.14.1, rel/0.7.15
fixed_in:
branch: feat/w51-npc
commits:
tests: sync_mobile
signature:
docs:
related: BUG-0438, BUG-0186, BUG-1138
---

## Report
The B1 impact run's sync_mobile (SPORE_NET=mobile: 120 ms, 3 % loss; coop_sync.py over every stage) failed only
"civ/vehicles: 90% within 3.0 m" (one vehicle, p90 4.18 m; feat/w51-retire 20261115-133237 lane 6, rel/0.7.15 5206247b
20261115-141528).

## Root cause
Not a regression of 0.7.15: v0.7.14.1 fails the same in all three runs today (20261115-154901 lane 3: p90 4.14 m;
20261115-205507 lane 3, 2 of 2: p90 5.08 m), rel/0.7.15 too (20261115-154901 lane 2: p90 4.20 m); every other
object kind of every stage passes. The lab ran at a load of 30-40 on 12 cores the whole day (other packages' runs).
This is BUG-0438's class (closed not-reproduced on 2026-11-07: the Civ mirror's error grows with the host's state age,
which a loaded lab raises; body_trail_civ_loss passed at a moderate load), in civ.cpp, outside this package's files.

## Fix
None here: duplicate of BUG-0438. For the final check: run sync_mobile on a quiet lab; if it still fails there, reopen
BUG-0438 for the Civ package (the vehicle state's age, not a test threshold).

## Verification
no-fail-run: not a 0.7.15 regression (FAIL on v0.7.14.1: 20261115-154901, 20261115-205507; FAIL on rel/0.7.15:
20261115-154901 lane 2), duplicate of BUG-0438.

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-npc): compared with v0.7.14.1 on the same lane, closed as a duplicate of BUG-0438.
