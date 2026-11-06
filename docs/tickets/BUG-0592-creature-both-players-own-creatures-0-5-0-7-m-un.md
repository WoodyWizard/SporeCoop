---
id: BUG-0592
title: Creature: both players' own creatures 0.5-0.7 m under the ground right after the guest's return from the main menu
status: fixed
kind: bug
stage: Creature
reported: 2026-11-05
source: lab long_creature_short 20261105-002102
role: both
versions: 0.7.7-dev
fixed_in: 0.7.8
branch: fix/w44-height
commits: 9ce58786
tests: own_ground
signature:
docs: docs/re/creature.md#creature-height-rule
related: BUG-0504, BUG-0425
---

## Report
The long-session bot (long_creature_short, rel/0.7.7 74f4ad0d, lab out/lab/20261105-002102) failed on the monitor's
ground gate: "host: its own creature 0.70 m under the ground (Creature) on 2 lines from s 380" and the same on the
guest (0.52 m), right after the guest's "menu" transition (exit to the main menu, join again, 16 s). Before it the
session had an editor transition. Not seen in the same test's earlier runs (D0 branch).

## Root cause
Not analysed yet. Both games' own creatures at once suggests the game's own ground (terrain) state after the
guest's world load, not a puppet; the host did not leave.

Update (2026-11-05, final check out/lab/20261105-015350 long_creature, full 40 min): the same gate on the host at
s 1400, again right after the guest's "menu" transition (which followed an editor one). The host's trace: at s 1390
the avatar stood at (-219.8 -454.1 -48.6) agl 0.00; the host saved the world for the share at s 1392; at s 1400 and
1410 it stood at (-219.5 -453.5 -48.6) agl -0.69: moved 0.7 m sideways at the same height, not sunk. At s 1420 it
walked and was back on the ground (agl 0.00). So something moves the standing avatar on the share's save or the
guest's join (a push by the partner's puppet when it is shown again is the first suspect), and the game puts it on
its ground only once it walks. Visual, 20 s, recovers by itself; not a release blocker for 0.7.7.

Fix analysis (2026-11-05, fix/w44-height):
* Observed (report's trace, lab 20261105-015350): the guest comes back from the menu where the host's save has the host's
  creature (its puppet appears 0.2 m from the host's, `d=0`); the host's standing creature is then 0.7 m to the side at
  the same radius, agl -0.69, until it walks.
* Observed (coop_own_ground.py): two own creatures put 0.3 m apart on a slope: the game's separation left the guest's
  own creature 0.35 m under the ground (lab 20261105-113544, fix off); a standing creature moved 0.7 m beside its
  spot and 0.69 m under the terrain there stays there with its grounded byte +0x137 still 1 (9 of 9 sample pairs
  under 0.25 m, 4 s); 0.32 m under, or moved by SetPosition, the game puts it back at once.
* Observed with the fix (regression long_creature_short, lab 20261105-113902, host): 0.9 s after "Bob appeared in the
  world" (the share after the guest's transition) `the own creature stood 0.56 m under this game's ground for 0.5 s
  (grounded byte 1, nearest puppet 0.8 m): put onto it`; twice more at the start (0.27-0.30 m, the puppet 0.9-1.0 m
  away). So the partner's creature arriving at the host's spot pushes the host's into the ground, the report's case.
* Inferred: the push is the game's separation of the two creatures; not seen frame by frame.

## Fix
creature.own_ground (modes.cpp own_ground_tick, `coop.fix creature.own_ground off`: 0.7.7): the own creature that stands
(under 0.3 m/s), is alive and more than 0.25 m under GetHeightAt for 0.5 s is placed onto it (game::place), on each
game; logged (`creature: the own creature stood X m under ...`, the grounded byte, the nearest puppet).

## Verification
own_ground (coop_own_ground.py): FAIL with SPORE_FIX_OFF=creature.own_ground out/lab/20261105-113544 (host and guest
0.69 m under for 4 s, the overlap phase 0.35 m); PASS out/lab/20261105-113703 (put back within 0.5 s, min 0.00 m).

## History
- 2026-11-05: opened.
- 2026-11-05 (fix/w44-height): fixed (creature.own_ground); own_ground FAIL/PASS.
- 2026-11-05: released in 0.7.8.
