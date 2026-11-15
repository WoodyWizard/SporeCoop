---
id: BUG-1056
title: Civ: while the host is away the guest's game still skips the ground check of its mirror vehicles, which its own AI drives then
status: fixed
kind: bug
stage: civ
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-14
role: guest
versions: 
fixed_in: 0.7.14
branch: fix/w49-small
commits: f4aca716, 1bc2f326, 3e2de219
tests: civ_away_ground
signature:
docs: docs/re/civ.md#civ-away-ground, docs/06-testing.md#testing-t1-small
related: BUG-0186, BUG-0151
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-14 (the only part that holds). Not seen at runtime, found by code reading.
While the host is away (main menu, editor), a guest's Civ vehicles are run by its own game's AI (BUG-0151,
civ.away). The guest's game still skips Vehicle_KeepOnValidGround for every mirror vehicle (BUG-0186), which was made
for vehicles that stand where the host's vehicle stands. With the AI driving them, a land or sea vehicle could end up
on ground its locomotion does not allow (in water, on a spice node) and stay there.

## Root cause
Confirmed at runtime: civ.cpp h_KeepGround skipped Vehicle_KeepOnValidGround for every mirror on a guest also while
the host was away (local_play), when the guest's own game drives the mirrors and no host state puts them back. A land
mirror put on the civ_npc fixture's spice node (ground the guest's game finds invalid for it, BUG-0186) stayed there
(0.2-0.4 m) while the host was in the menu.

## Fix idea
Add `!local_play()` to the skip. Switch idea `coop.fix civ.away_ground off`.

## How to test
civ fixture twoinst test: the host goes to the main menu, the guest orders its sea (or land) mirror vehicle across a
coast line its locomotion does not allow; assert on the guest's game that the vehicle's cell is valid for its
locomotion (the game's own planet cell flags) after the drive. Main: it stands on invalid ground: FAIL (if the AI's own
path can produce it at all; otherwise close as not reproduced with the run).

## Fix
Owner decision 8 (check whether KeepGround needs a local-play bypass): it does not; with the evidence above the game's
own check runs again for mirrors while the host is away (`!local_play()`): the game moves the vehicle 13 m off the
node, as the host's game would. While the host plays the skip of BUG-0186 stays. Switch `coop.fix civ.away_ground off`
(0.7.13). The test command `coop.civ vput` works on a guest in local play (StopMovement first, as guest_tick does).

## Verification
Test `civ_away_ground` (coop_civ_away_ground.py: civ_npc, the host in the menu, the guest puts an NPC land mirror on
the spice node three times; 0.3 s later it must be over 6 m off).
FAIL (SPORE_FIX_OFF=civ.away_ground): out/lab/20261113-031244 (0.2-0.4 m, `away_ground=0`). PASS: out/lab/20261113-031145-2,
out/lab/20261113-031904 (3.0 / 12.9 / 13.1 m). (Run out/lab/20261113-030613-2 is void: the test command's SetPosition was
pulled back by the locomotion; fixed in the test command before the runs above.)

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-small (package T1).
- 2026-11-14: released in 0.7.14.
