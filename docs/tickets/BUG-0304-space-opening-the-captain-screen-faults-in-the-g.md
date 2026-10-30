---
id: BUG-0304
title: Space: opening the captain screen faults in the game at 0x5971D4 (cCollectableItems method with a null this), also without coop
status: game-bug
kind: crash
stage: space
reported: 2026-10-30
source: lab 20261030-075052 campaign#3
role: 
versions: 0.7.3.1
fixed_in:
branch: fix/w11-finale
commits:
tests: space_captain
signature: 0xC0000005 at 0x005971D4 Simulator::cCollectableItems::LockPart+0x3C4 (data 0x4D04), ecx/esi/edi 0, calls 005BF210 00F17A11 00F17A1E
docs: docs/re/space.md#space-captain-screen-fault
related: BUG-0303
---

## Report
Lab 20261030-075052 campaign#3 (main 390f73d, lane 7): `FINDING [fault] Carol: 2 caught faults`,
`0xC0000005 at 0x005971D4 Simulator::cCollectableItems::LockPart+0x3C4 (data 0x00004D04)`, ecx = esi =
edi = 0, during the Space play phase on the extra guest. The game handled it and played on. The
screenshots of that moment show the captain screen open (pink "*UNKNOWN<ep1_captain_primary_title>*"
panel, Equipment, Crew, OK). Seen once in the lab history before (none in any other out/lab log).

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

## Fix
None in the mod (the game handles the fault; a player sees no crash). The test bot no longer opens
the screen by accident (BUG-0303). `space_captain` opens it on purpose and checks that this is the
only caught fault (suite.py reports=True).

## Verification
no-fail-run: a game bug, nothing fixed. Reproduced: lab 20261030-095947-fix-w11-finale, 20261030-100128-fix-w11-finale,
20261030-100242-fix-w11-finale (space_captain: "guest faults after opening it: 1"); by hand on lane 2 with coop unloaded.

## History
- 2026-10-30: opened; reproduced on fix/w11-finale (also with coop unloaded): the game's own fault, game-bug.
