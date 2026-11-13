---
id: BUG-0997
title: Creature, Tribe: npc.place_now switches off the snap of a walked NPC proxy (a host animal thrown aside is closed at catch-up speed, not teleported)
status: open
kind: desync
stage: Creature,Tribe
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-15
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-npc-snap-dead
related: BUG-0757, BUG-0361
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-15. Not seen at runtime, found by code reading. A guest's copy of a walking
host animal (Creature and Tribe NPC proxies) is walked by the game and placed on the host's path (drive::walk_place).
When the host's animal jumps aside (thrown by a hit, put back by its game) the copy is meant to be teleported when it is
over 2.5 m off (`o.snap`, BUG-0361). Since npc.place_now (BUG-0757, 0.7.10) the proxy is aimed one frame ahead along
its velocity, and that aim no longer equals the path point recorded by path_update, so the snap's "the path jumped
this frame" test never holds for a moving proxy: the copy slides back to the host's animal at the catch-up speed
(up to ~2 s at 2-7 m off, what BUG-0361 measured before the snap) instead of jumping there.

## Root cause
Observed in code (main efd2e0ba):
* mods/coop/npc.cpp:2083 `drive::path_update(px.drv, s.pos, now)` records `ds.path.prev = s.pos`.
* npc.cpp:2141-2149: `o.snap = 2.5f`; with g_place_now_fix `aim = s.pos + vel * g_frame_dt`, then
  `drive::walk_place(px.c, aim, ...)`.
* mods/coop/driver.h:358-359 walk_place: the snap needs `net::dist(ds.path.prev, pos_in) < 0.01f`; pos_in is the aim,
  `|vel| * g_frame_dt` from prev (a proxy walks only above 0.3 m/s, npc.cpp:2129: 0.01 m or more at 30 fps and below).
Inferred (high confidence): the snap is dead code for NPC proxies while npc.place_now is on; the catch-up (catch_prop,
keep_lost) still closes the error, slower. Severity: medium-low (a visible slide of an animal copy after a hit or
knock-back in Creature and Tribe). civ.place_now (BUG-0438, civ.cpp) may have the same pattern: not checked here.

## Fix idea
Give walk_place the path point separately (a `path_pos` in WalkPlace, defaulting to pos_in) and compare that with
`ds.path.prev`; teleport to the aimed point. Switch: `npc.place_now_snap` (off: today's code).

## How to test
tools/tests/coop_npc_walk.py (suite npc_walk_player) or the npc tests that measure the guest's error after a host-side jump (BUG-0361's
npc_walk_player rows with coop.npctrack): the host puts a walking NPC 5 m aside (test command teleport while it walks);
assert on the guest's game that its copy is within 1 m of the host's within 0.2 s (the game's position, the driver's
teleport count from coop.npctrack only as a diagnosis). FAIL on main (seconds to close), PASS with the fix; with
npc.place_now off it PASSes on main too (the twin that shows the cause).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
