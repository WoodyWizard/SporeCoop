---
id: BUG-0264
title: Guests fault on a freed avatar while leaving the creature stage for a shared world after the evolution (dc04aee5:coop.dll+0x82858, game::max_health)
status: fixed
kind: crash
stage: creature
reported: 2026-10-29
source: lab 20261029-070811 campaign
role: guest
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w6-avatar
commits: 3f5d29e cb4ddd2 af73ab0 defb3ec
tests: leave_avatar (tools/tests/coop_leave_avatar.py), leave_avatar_cell (tools/tests/coop_leave_avatar.py --cell)
signature: dc04aee5:coop.dll+0x82858
docs: docs/re/creature.md#creature-leave-avatar, docs/06-testing.md#testing-leave-avatar
related: BUG-0091
switch: world.leave_avatar
---

## Report
Final 4-player campaign, lab run 20261029-070811, campaign#2 (lane 4, seed 958123806, run
out/lane4/campaign/20261029-070819). Cell -> Creature: the host evolved, every guest followed, then
the host shared its world a second time right after its creature editor. All three guests died
within a second of "world: leaving the stage to the galaxy menu before loading Cadian.spo":

* Bob (instance 8): `0xC0000005 at coop.dll+0x82858 (data 0x00000059)`, then a hang report
  (report-20261029-081902-i8.txt, copied into out/lab/20261029-070811/).
* Carol: `0xC000001D` at a heap address (a call into freed memory), then a freeze.
* Dave: `0xC0000005` at a heap address, then a freeze.

Bob's log, last lines: "Alice (host) is back" 25.2 s, "receiving world from Alice" 29.9 s, "leaving
the stage to the galaxy menu" 31.047, "save: 5 objects of ours are kept out of it" 31.111, the fault
32.105. The other two campaigns of the run passed the same change (lane 2 played on to Civ).

## Root cause
Observed: llvm-symbolizer on build/coop.dll (b4b747a): 0x10082858 = game::max_health (game.cpp
489, the vcall of cCombatant::GetMaxHitPoints), the return address on the stack 0x10088D33 =
modes::creature_fill (modes.cpp 43). creature_fill runs from local_state() every 50 ms with
g_local = game::avatar(), the noun manager's avatar field while the mode id is the creature stage.
The vtable read gave 0x01: the avatar object was freed or reset.

Inferred: after the quit to the galaxy menu is posted, the game saves and tears the stage down while
the mode id still reads Creature for about a second; the avatar field keeps the freed pointer.

Observed (fix/w6-avatar): Carol's and Dave's faults are the same path. Their stacks hold coop+0x8285B
(the return address after the vcall in max_health) and coop+0x88D33 (creature_fill): the freed
combatant vtable pointed into the heap, so the vcall jumped there (0xC000001D / 0xC0000005 at heap
addresses). Nothing else (teamplay, npc, cellsync) is on those stacks: everything reaches the avatar
through game::avatar() each frame, g_local is refreshed every frame from it.

Observed (lanes 4/5, 2 players): the leaving window (quit posted -> mode change) lasts 0.63-0.71 s;
our frame hook runs 3 times in it (right after the post, then twice after the game's ~0.65 s save).
In 30+ windows, also the campaign's way (Cell -> Creature, the host's first editor, its share after
it), the avatar stayed in the game's object list with valid vtables in every frame. The campaign's
fault came at 1.06 s, later than any window measured here: why its window was longer and its avatar
already freed (4 games per lane under the load of other lanes) is not known.

## Fix
* `game::avatar()` returns null while the stage is being left: from the quit message 0x4715068
  (posted by world.cpp post_quit_to_menu / coop.menu, or handled by Simulator_HandleMessage for any
  source, the player's own quit too) until mode_id() changes, 30 s at most; a via-menu load that is
  cancelled clears it. Everything that reads the avatar (g_local, creature_fill, teamplay, npc,
  creations, world_epoch) goes through avatar(), so the whole mod lets go of it.
* A cheap liveness check: an avatar whose object or combatant vtable is outside the exe is never
  returned (the freed avatar's vtable read 0x01).
* `game::avatar_field()`: the raw field for pointer comparisons only (the save filter runs inside
  the quit's own save and must still recognise the avatar).
* Diagnostics: `coop.leaving` (window count, frames, avatar unlisted / used while unlisted, refused
  calls, bad vtables), a "leaving frame" log line per frame in the window, and a 1 Hz check in the
  stage that logs an avatar the game no longer lists.
* Switch: `coop.fix world.leave_avatar off` (the avatar field as it is).

## Verification
no-fail-run: the freed avatar did not show with 2 players. With `SPORE_FIX_OFF=world.leave_avatar`
(FAIL side) the tests passed: lab runs 20261029-085208-fix-w6-avatar (leave_avatar, 4 rounds),
20261029-085452-fix-w6-avatar (leave_avatar x2, 12 rounds), 20261029-085805-fix-w6-avatar
(leave_avatar_cell x2: Cell -> Creature, the host's editor and its share, 3 more rounds each): 24
creature leaving windows, 72 frames, the avatar listed in all of them. The tests measure the game's
own object list in the window, so they would catch it where it happens.
* PASS (fix on): 20261029-090214-fix-w6-avatar (leave_avatar x2, leave_avatar_cell x2): avatar() null
  in every window frame (~237 calls refused per window), no fault.
* PASS (final code, with the save filter on the raw field): 20261029-092156-fix-w6-avatar
  (leave_avatar, leave_avatar_cell). Smoke: 20261029-090922-fix-w6-avatar 14/14 (lanes 4, 5).

## History
- 2026-10-29: opened.
- 2026-10-29: fixed on fix/w6-avatar (3f5d29e, defb3ec): no avatar while the stage is left; test
  leave_avatar / leave_avatar_cell, no-fail-run (not reproduced with 2 players).
- 2026-10-29: released in 0.7.3.1.
