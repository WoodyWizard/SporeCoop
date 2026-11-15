---
id: BUG-0656
title: Tribe: the tribe camera set-up is skipped without a simulator camera (tribe_theme_music, tribe_hit_impacts)
status: duplicate
kind: bug
stage: Tribe
reported: 2026-11-06
source: lab log sweep 2026-11-06 (703 runs, 2026-11-05/06)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in: 
branch: fix/w46-s1-sweep
commits: 1081cd4f
tests: 
signature:
docs: docs/06-testing.md#testing-log-signals
related: BUG-0167, BUG-0607, BUG-0626
---

## Report
5 runs: `game fixes: skipped the tribe camera set-up without a simulator camera` (fix/w44-tribe-guest out/lab/20261105-115653/tribe_theme_music.guest.game.log:110; fix/w45-tribe tribe_hit_impacts). Related: BUG-0167.

## Root cause
Observed: 7 of 7 lines (2026-11-01..05: tribe_coop x2, npc_walk_tribe, tribe_save_strip, tribe_theme_music, tribe_hit_impacts x2)
came 0.06-0.15 s after "stage: Loading -> Tribe" and 1 s before "cinematic 8915F9BB still playing in a stage": the galaxy
menu intro played into the stage and held the camera, so the simulator camera object [0x167FAAC] was null when TribeGame_Setup
called LookAt; the guard (gamefixes.cpp lookat_guard, the crash of BUG-0167) skipped it. That intro in the stage is BUG-0607 /
BUG-0626 (fixed in 0.7.9 by world.intro_at_once): 0 lines in 1905 Tribe loads of the runs since 2026-11-06 13:00 (3847 Tribe loads
before had the 7).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
The guard stays (BUG-0167). The line now gives the reason: "(mode %08X, cinematic %08X)" (world::cinematic_id).
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: the cause is BUG-0626 (fixed, test tribe_intro_camera); counts from the lab logs above.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the orchestrator's lab log sweep; planned for 0.7.10.
- 2026-11-07 (fix/w46-s1-sweep): duplicate; see Root cause / Verification.
