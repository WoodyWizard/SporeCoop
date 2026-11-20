---
id: BUG-0304
title: Space: opening the captain screen faults in the game at 0x5971D4 (cCollectableItems method with a null this), also without coop
status: fixed
kind: crash
stage: Space
reported: 2026-10-30
source: lab 20261030-075052 campaign#3; GitHub issue #203
role: guest
versions: 0.7.3.1, 0.7.15.1
fixed_in: 0.7.15.3
branch: fix/w11-finale, fix/w55-captain
commits: 045af236, e8cb2c3c, 98ba8407
tests: space_captain, space_captain_none
signature: dc04aee5:SporeApp.exe SporeCoop::CollectableItems_5970D0+0x104 (0xC0000005 at 0x005971D4, read of 0x4D04, ecx 0; was named Simulator::cCollectableItems::LockPart+0x3C4), calls 005BF210 00F17A11 00F17A1E
docs: docs/re/space.md#space-captain-screen-fault, docs/re/space.md#space-captain-items-null
related: BUG-0303, BUG-1111, BUG-1278, BUG-1279, BUG-1254, BUG-1262
---

## Report
Lab 20261030-075052 campaign#3 (main 390f73d, lane 7): `FINDING [fault] Carol: 2 caught faults`,
`0xC0000005 at 0x005971D4 Simulator::cCollectableItems::LockPart+0x3C4 (data 0x00004D04)`, ecx = esi =
edi = 0, during the Space play phase on the extra guest. The game handled it and played on. The
screenshots of that moment show the captain screen open (pink "*UNKNOWN<ep1_captain_primary_title>*"
panel, Equipment, Crew, OK). Seen once in the lab history before (none in any other out/lab log).

GitHub #203 (0.7.15.1, GOG 2024 dc04aee5, Windows 10 build 26200, a guest in the host's Space world, the game run
with `-locale:ru-ru`): "the game crashes when I click the about-captain button; it also crashes when I accept an
adventure mission. Every time." Five crash reports, every one `SporeCoop::CollectableItems_5970D0+0x104`
(0xC0000005 at 0x5971D4, read of 0x4D04, ecx 0), **unhandled**, the game's main thread, return 0xF17A1E; one chain
through SpaceGameUI_ShowCaptainScreen (the rank badge, return 0xFE7973), one through the window manager (Accept,
return 0xFE7F74). The guest's log: the host's captain 292845D0 is one of the host's own creations, registered on the
guest ("the host's own creation 292845D0 ... is 7DB7E46D here"). On Wine the game handled this fault (the lab's
"game-bug" of 2026-10-30); on this Windows PC it ends the game.

## Root cause
Observed (disassembly of the GOG 2024 exe, runtime on lane 2):
* 0x5971D4 is not in LockPart (0x596E10, a short function): it is the out-of-line block of the next
  function, 0x5970D0 `thiscall (this, int n)`, `ret 4`: `mov edi, ecx` ... `jle 0x5971D4` when n <= 0,
  then `mov ecx, [edi+0x4D04]`. It touches `this+0x4D00..0x4D08` like LockPart does, so it is a
  cCollectableItems method; it was called with this = null and n = 0.
* The caller is 0xF179E0 (`call 0x5970D0` at 0xF17A19, return 0xF17A1E in the fault's call list):
  it gets an object with 0x5BF1E0 / 0x5BF2E0 (a lookup by a key, the result written to a local) and
  calls 0x5970D0 on it without a null check. Another caller (0xF16CED, push 0) has the same pattern.
* The click on the rank badge over the ship's picture (bottom right, 1390 890 at 1440x1080) opens the
  captain screen; each opening logs one caught fault at 0x5971D4 with the same call list, on the host
  and on the guest of the space fixture, and **also with `mod.unload coop`** (3 openings, 3 faults on
  lane 2): the game's own code, not coop's. The game's handler catches it and the screen stays up.
Inferred: the collectable items (the captain's parts) do not exist in these saves (a Space game
without a Galactic Adventures captain), so the lookup returns null. Not checked: a save with a GA
captain, the 2016 build.
How the screen came up in the campaign: the Space play bot clicked a planet projected under the badge
(BUG-0303).

CORRECTION (2026-11-18, fix/w55-captain, static, Ghidra GOG 2024; docs/re/space.md#space-captain-items-null):
* The item list comes from the captain's property list {captain, 0x00B1B104, group | 0xE500} (0x5BF1E0) and its
  property 0xC339D5C7 (0x5BF2E0); it is null when this game has either missing. The reporter's stack at the fault
  holds 292845D0 / 00B1B104 / 4062E500. Not "every Space game without a GA captain": any captain whose property list
  this game lacks, and **always a guest in the host's world**: coop sends the host's own creation (model, metadata,
  pictures, creaturedata of its group), not that resource (BUG-1279).
* Both routes of the report go through the captain panel 0xFE7560 (mode 0 the badge, mode 2 Accept, control
  0x075DD108 of 0xFE7CF0) into the items palette 0xF179E0 (call 0xFE796E).
* The game's other users treat a null list as "no items" (0x5C6820, 0x5C66E0, 0x5BF710, 0xFDBF80); only 0x5970D0
  (callers 0xF17A19, 0xF16CED, 0xF125BB) dereferences it.
* The locale does not matter (a lookup by key, no string). Why Wine handles the fault and Windows does not: not known.

## Fix
fix/w55-captain (0.7.15.3): gamefixes.cpp `game.captain_items` hooks SporeCoop::CollectableItems_5970D0 (thiscall(list,
n) ret 4, void): a null list returns at once (the game's "no items"; the palette then shows no item limits), a warning
names the captain. `coop.fix game.captain_items off`: the game's code (0.7.15.2). The next fault of the same panel
once this one is gone (the stats of a captain without a species profile, 0xC02A14) is BUG-1278
(`game.captain_stats`). The real data the guest lacks (the host captain's property list) is BUG-1279.

Before (2026-10-30): None in the mod (the game handles the fault; a player sees no crash). The test bot no longer opens
the screen by accident (BUG-0303). `space_captain` opens it on purpose and checks that this is the
only caught fault (suite.py reports=True).

## Verification
* Lab 2026-11-19 (lane 3, fix/w55-captain): FAIL `SPORE_LAB_FIXOFF="space_captain=game.captain_items"`
  20261119-112022 (the guest's caught fault `0x005971D4 SporeCoop::CollectableItems_5970D0+0x104` after Accept and
  after the rank badge); PASS space_captain 2x and space_captain_none 2x 20261119-111406 (`items: none` +1 per
  opening: the guard ran, no caught fault on either game).
* The first lab run 20261119-094350 failed in the test, not the mod: `coop.ui find "Accept"` returns the captain
  panel's hidden copy, so the shown mission screen was missed and, left open, kept the badge from opening the
  captain screen (fix-off 20261119-094613 failed the same way, not for the bug). The tests find the mission screen's
  Accept by control id 0x075DD108 now (`uifind.where`).
* Before (2026-10-30) no-fail-run: a game bug, nothing fixed. Reproduced: lab 20261030-095947-fix-w11-finale, 20261030-100128-fix-w11-finale,
20261030-100242-fix-w11-finale (space_captain: "guest faults after opening it: 1"); by hand on lane 2 with coop unloaded.

## History
- 2026-10-30: opened; reproduced on fix/w11-finale (also with coop unloaded): the game's own fault, game-bug.
- 2026-11-18: reopened (game-bug -> open): GitHub #203 (0.7.15.1, Windows): the same fault is fatal there, on the
  rank badge and on Accept of an adventure mission; null list root cause (the host captain's property list is not
  on the guest), guard game.captain_items, test space_captain requires no fault (fix/w55-captain).
- 2026-11-19: lab: the tests found the mission screen by a hidden caption (fixed: control id); PASS 20261119-111406,
  FAIL with the guard off 20261119-112022; known-issues entry archived (fix/w55-captain).
- 2026-11-19: released in 0.7.15.3.
