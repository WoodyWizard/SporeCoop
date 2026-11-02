---
id: BUG-0444
title: tribe: in tribe_coop the guest's own tribe members are 2.6-3.3 m under its ground for ~20 s (monitor rule ground)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-01
source: tribe_coop (smoke), lab 20261101-183414 (feat/w33-tests-session, main 6ac351e merged)
role: guest
versions: main 6ac351e
fixed_in: 0.7.3.4
branch: fix/w36-tribe-ground
commits: f606ce34
tests: tribe_mirror_ground, tribe_coop
signature: guest trace agl=<-0.5 for its own members with ai_skipped=0/0 (game time paused)
docs: docs/re/tribe.md#tribe-mirror-ground, docs/06-testing.md#testing-tribe-mirror-ground
related: BUG-0399, BUG-0329, BUG-0332, BUG-0365
---

## Report
`tribe_coop` (smoke, coop_tribe.py, tribe fixture, cold, lane 10) passed its own checks and failed by main's
monitor rule `ground` (feat/w32-monitor-diag): `MONITOR-VIOLATION ground guest: the tribe members of guest 3.31
m under the ground (Tribe) on 2 lines from s 61`. The guest's trace lines: `agl=-2.64` (s 60.6) and
`agl=-3.31` (s 70.1), 0.00 before and after; the minutes of the test where the host walks the guest's member
9 m, gives action and hunt orders (lab 20261101-183414). The run before on lane 9 (lab 20261101-180254) failed
two UI checks instead (box select, right-click on a tree), not this rule; tribe_coop passed 162 of 165 runs
before the rule existed.

How to reproduce: `tools/lab.py run tribe_coop --repeat 4 --retry 0 --lane N`, the guest's `agl=` lines.

## Root cause
Observed (lane 10, tribe fixture): after a cold join the guest's game time stays paused (`cGameTimeManager`
+0x48 = 3, no BehaviorTreeTick: `ai_skipped=0/0`) for ~15-30 s; nothing but the mod moves or grounds the mirrors
then. The mirrors were driven at their own distance from the planet's centre (own_height, BUG-0329) and never put
onto this game's ground (that was left to the game's locomotion), so the guest's own member, walked by the host
60 m up a 5-12 m rise while the guest's game was paused, went 9.2-9.8 m under the guest's ground (back down the
slope: up to 10 m over it); a new mirror was teleported to the host's absolute position (2 m under with
`coop.tribe lift -2`). Resumed, the game grounded it within a second.
Inferred for the reported run (lab 20261101-183414): the guest's BT ticks were still 0 at s 40.6 and only 2394 at
s 80 (passing runs 665-6705 at s 50), i.e. its game was paused into the hunt order, whose run took member [2]
~220 m away: the 2.6-3.3 m under at s 60-70. No per-frame data of that run. Rare in tribe_coop (1 of 152 recorded
Tribe guest logs with `agl_hi`), so tribe_coop alone gives no reproducible rate.

## Fix
`tribe.mirror_ground` (tribe.cpp): the ground policy of the animal proxies (npc.proxy_ground, BUG-0399): a new
mirror is made on this game's ground (`drive::out_of_ground`), the walk aim is put out of the ground and
walk_place's `ground` option is on, a standing mirror found more than 0.25 m under the ground is teleported onto
it (drive::step's `ground`, which is now the caller's switch: npc.cpp passes npc.proxy_ground). While this game's
time is paused the aim also comes down onto the ground from above (`drive::onto_ground`, within 15 m), because
nothing else grounds it then. `coop.tribe lift` now applies to a new mirror's position too (tests).
`coop.fix tribe.mirror_ground off`: own height only (0.7.3.3).

## Verification
* FAIL (`SPORE_FIX_OFF=tribe.mirror_ground`): out/lab/20261101-191623 tribe_mirror_ground 3 of 3 cold runs
  (walked mirror 9.2-9.8 m under, up to 9.8 m over, new mirrors at -2.0 m; the monitor's `ground` rule in one).
* PASS: out/lab/20261101-191251 tribe_mirror_ground 3 of 3 (1 cold, 2 warm): -0.12 to 0.02 m, new mirrors
  -0.11 m.
* Regression (lane 10, gates on): out/lab/20261101-192144 (tribe, tribe_play, tribe_walk, tribe_walk_player,
  npc_walk_tribe 5/5), 20261101-193010 (tribe_coop x3, chaos_tribe x3 6/6; the guests' lowest `agl` -0.03 /
  -0.05 m), 20261101-194242 (smoke 13/14, `ui` skipped: needs lanes 1-5).

## History
- 2026-11-01: opened.
- 2026-11-01: fixed on fix/w36-tribe-ground (tribe.mirror_ground), test tribe_mirror_ground.
- 2026-11-02: released in 0.7.3.4.
