---
id: BUG-0997
title: Creature, Tribe: npc.place_now switches off the snap of a walked NPC proxy (a host animal thrown aside is closed at catch-up speed, not teleported)
status: fixed
kind: desync
stage: Creature,Tribe
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-15
role: guest
versions: 
fixed_in: 0.7.14
branch: fix/w49-small
commits: f4aca716
tests: npc_snap
signature:
docs: docs/known-issues.md#issue-npc-snap-dead, docs/06-testing.md#testing-t1-small
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
Confirmed at runtime: with npc.place_now the walked proxy was aimed a frame ahead and drive::walk_place compared the
aim (not the path point) with ds.path.prev: the snap never fired for a moving proxy; a host NPC put 5 m aside was
closed by the catch-up in 0.39-0.78 s (median 0.46 s) on the guest's screen.

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
WalkPlace takes this frame's path point (has_path_pos / path_pos) for the snap's "the path jumped" test; the teleport
still goes to the aimed point. Switch `coop.fix npc.place_now_snap off` (0.7.10 .. 0.7.13). Test command
`coop.npctrack shove <id> <m> x y z <m/s>` (host).

## Verification
Test `npc_snap` (coop_npc_snap.py: three host NPCs walking at 3 m/s around the guest, each put 5 m aside three
times; the guest's per-frame track gives the time until its drawn copy is within 1 m of the host's path).
FAIL (SPORE_FIX_OFF=npc.place_now_snap): out/lab/20261113-031433 (median 0.46 s, slowest 0.78 s). PASS: out/lab/20261113-030613,
out/lab/20261113-031904 (median 0.03 s, slowest 0.04 s in the first).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-small (package T1).
- 2026-11-14: released in 0.7.14.
