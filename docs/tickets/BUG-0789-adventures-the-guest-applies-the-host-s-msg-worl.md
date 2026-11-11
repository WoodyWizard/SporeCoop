---
id: BUG-0789
title: Adventures: the guest applies the host's MSG_WORLD_TIME planet orientation, which overrides the adventure's authored lighting
status: open
kind: bug
stage: Adventure
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-main e43c1484 (preview 8 local lighting)
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
No player report. In an adventure the guest's lighting / sky follows the host's planet orientation instead of the
adventure's own authored lighting; the guest's sky can jump when the host moves (the fork measured a 4.95 degree change
of the host's planet orientation while the host walked 63 m, the guest standing still).

## Root cause
Observed in our code, impact from the fork's analysis (not verified by us; confidence medium-high):
the host sends MSG_WORLD_TIME every second in any mode (`mods/coop/coop.cpp:3556-3565`, `game::time_of_day` reads the
orientation of the time-of-day object, i.e. the active planet), and the guest applies it whenever it has an avatar
(`coop.cpp:4386-4390`, gated only by `wsync::owns_time_of_day()`), also in kScenario (adventures). The fork's RE:
`TimeOfDay_GetObject=0x1021110` returns the active planet; native `cTimeOfDay::SetTimeOfDay=0xBC3070` thiscall
(float time, Vector3* position) ret 8; helper 0xBC2DC0 computes the planet orientation from the time AND the position,
then SetOrientation + 0x1021630. So the quaternion is position-dependent, not a clock: applying the host's to another
captain (another place, maybe another adventure planet) gives that guest wrong lighting. Also applied when host and guest
are in different worlds or modes (no world check on the receiving side).

## Fix idea
From the fork (jwhans0n/SporeCoopGA e43c1484, docs/re/adventure.md "Downed captain controls and local lighting"); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
They added `legacy_world_time(mode)` = mode != 0x01654C10 (kScenario) and gate both the host's send and the guest's
apply with it, so adventures keep native local lighting; packets delayed across entering an adventure are dropped too
(the check is on the receiver's current mode). Other stages keep the old sync. For us also consider dropping the packet
when the sender's world / mode differs from ours.

## How to test
Two games in the same adventure, guest standing still, host walks ~60 m. Read the planet orientation (time-of-day object
+0x10) on the guest every second. FAIL: the guest's quaternion follows the host's (changes by degrees); PASS: it stays at
the guest's own native value (changes only with its own clock).

## Fix
## Verification

## History
- 2026-11-10: opened.
