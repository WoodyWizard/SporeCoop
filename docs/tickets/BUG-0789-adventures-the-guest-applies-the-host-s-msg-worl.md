---
id: BUG-0789
title: Adventures: the guest applies the host's MSG_WORLD_TIME planet orientation, which overrides the adventure's authored lighting
status: fixed
kind: bug
stage: Adventure
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-main e43c1484 (preview 8 local lighting)
role: 
versions: 
fixed_in: unreleased
branch: fix/w48-small
commits: 3a3c5d3b
tests: adventure_duo, adventure_duo_host_first
signature:
docs: docs/re/adventure.md#adventure-two-captains, docs/06-testing.md#testing-p1-g1
related:
switch: wsync.adventure_sky
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
Measured first (lab lane 2, two games in the tutorial adventure): the sky does depend on the captain's place: the host's captain put 200 m away moved the host's sky by 13.8 degrees (1.3 in another run, 0.52 for 60 m). In the default build the guest already kept its own sky: since BUG-0579 (`wsync.tod`, on) a guest does not apply MSG_WORLD_TIME at all, and the wsync sky row is not in play in kScenario. Only with `wsync.tod` off (0.7.8 and older) did the guest take the host's sky (0.0 degrees from it). The fix makes adventures independent of wsync.tod: `wsync.adventure_sky` (coop.cpp): the host sends no MSG_WORLD_TIME while its game is in kScenario, and a guest in kScenario applies none (a packet that crossed the guest's entry into an adventure is dropped too). `coop.fix wsync.adventure_sky off` with `wsync.tod off`: the legacy packet in an adventure, as before. No player-visible change for the default build (no CHANGELOG line).

## Verification
adventure_duo (`coop_adventure_duo.py`): host and guest join at the galaxy menu, both play the tutorial adventure; the host's captain is put 200 m away; 12 s later the guest's sky must be more than half of the host's sky's change away from it.
* FAIL with SPORE_FIX_OFF=wsync.tod,wsync.adventure_sky: out/lab/20261112-122759 (the host's sky moved 13.82 degrees, the guest's followed it: 0.00 degrees apart), also out/lab/20261112-122031 (first version of the test).
* PASS: out/lab/20261112-123202 (the guest's own sky, 174 degrees from the host's, unchanged while the host's moved 13.8), out/lab/20261112-124436 (adventure_duo_host_first, with puppets: host moved 1.3, the guest's 0.0).

## History
- 2026-11-10: opened.
- 2026-11-12 (fix/w48-small): measured; the default build already kept the own sky (wsync.tod); wsync.adventure_sky guards the legacy packet in adventures; test adventure_duo.
