---
id: BUG-0425
title: class: remote body placement, catch-up and snapshot timing (umbrella)
status: open
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
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
Done: shared drive::walk_place for puppet, Tribe mirrors, NPC proxies (BUG-0394), Space pin (BUG-0378).
Siblings not covered: WalkPlace::ground never enabled (driver.h:217): NPC proxies and Tribe mirrors not put on this game's ground (BUG-0399, downhill falls inferred); trail/stall only for NPC proxies (tribe.cpp:1026/1043, puppets stall=false; BUG-0305); Civ mirrors no own-ground; Space ship mirrors lack speed cap / extrapolation limit (squadron.cpp defaults; fits BUG-0190, 0388); Civ/Tribe accept a late chunk of an OLDER cycle (civ.cpp:1858, tribe.cpp:835) -> objects recreated (obs); no NaN/Inf guard on received poses outside Cell (game.cpp:823-840) (obs); oversize unreliable messages dropped silently (connection.cpp:162-165) (obs); root motion of abilities (BUG-0414); hops (BUG-0376); Tribe standing mirrors still on drive::step.
General fix: one driver policy for every remote body (ground, trail/stall, root motion, speed cap, extrapolation); one CycleTracker; NaN guard in game::teleport/set_position/place; count/log oversize messages. Outcome check: monitor drawn/ground/proxy rules for every kind at 30 fps + jitter; chunk unit test.
Source: docs/generalisation-plan.md G8.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
