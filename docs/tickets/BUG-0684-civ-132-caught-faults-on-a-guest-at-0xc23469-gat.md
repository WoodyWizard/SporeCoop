---
id: BUG-0684
title: Civ: 132 caught faults on a guest at 0xC23469 (gather node, null bundle in 0xC23460) next to an old tribe village, wobj carried bundles
status: fixed
kind: crash
stage: Civilization
reported: 2026-11-07
source: lab 20261107-141346 play_civ (rel/0.7.10 e7d1734c, lane 3)
role: guest
versions: 0.7.9+
fixed_in: unreleased
branch: fix/w46-k7-findings
commits: 9eaf50a3
tests: civ_village_bundle
signature: 0xC0000005 at 0x00C23469 (labelled SporeCoop::Citizen_IsLeader+0x39, really the function 0xC23460) data 0x34, edi 0; calls 00D74D66 00D74DA0 00D74DBD 00B38500 00B384A5 00B212EF (gather node state 0x10 -> 0xC23460(null bundle))
docs: docs/re/tribe.md#tribe-carried-bundles, docs/re/tribe.md#tribe-bundles-outside-tribe
related: BUG-0548, BUG-0617, BUG-0539
---

## Report
play_civ on rel/0.7.10 (lab 20261107-141346, lane 3) failed on `guest faults: total faults: 132` (and on "no problems
(1)", the spice node owner, BUG-0325's class). The guest logged one fault line at s 260.3 that repeated 132 times
(same eip), with a 2.0 s hitch, until the host's old tribe village next to it was gone (s 262.2, BUG-0539 removal):

    exception 0xC0000005 at 0x00C23469 SporeCoop::Citizen_IsLeader+0x39 (data 0x00000034) ... edi=00000000 esi=1004BF50
    calls: 00D74D66 00D74DA0 00D74DBD 00B38500 00B384A5 00B212EF 00BCC6CC 00CAD8EC 00B77EED 00B4C6D0

(play_civ.guest.game.log line 242). 6 s before it the guest made the host's carried bundle (5.0 food) at
(-277 389 219) and 1 s later the host's ground bundle at (-222 408 246), 25-35 m from the old village at (-244 405 231).
The fault address was never seen before in any lab log of any version.

## Root cause
* Observed (disassembly, GOG 2024): the symbol is wrong, Citizen_IsLeader is 0xC23430..0xC2345A; 0xC23469 is in the
  next function 0xC23460 (`mov edi,[esp+0x2c]; mov eax,[edi+0x34]`: its bundle argument is null). Its caller at
  0xD74D40 is a gather node sub-state of a tribe member (esi): 0xC15050 (the pick-up) returns true, then 0xC22950,
  0xCEE490(c+0x34) = the first bundle of the member's own container (null: empty), 0xC229C0, 0xC23460(bundle, -1);
  the fault leaves the state at 0x10, so the same tick faults every frame.
* Observed (code, worldobj.cpp wobj.bundle / wobj.bundle_carried, unchanged since v0.7.9): on a guest, a bundle
  carried by a creature of the guest's game that has no host partner is removed after kOwnGrace snapshots
  (`.drop_own`, bundle_own() false with wobj.bundle_carried, RemoveBundles), and a host's carried bundle is put into
  the container of the nearest empty-handed creature within 8 m. Both assume that the guest's tribe members are
  copies that run no behaviour of their own (true in Tribe). In Civ the old tribe villages run their own behaviour
  in each game.
* Inferred: a member of the guest's own old village carried food (or was handed the host's carried bundle); the
  bundle was removed from its container while its gather node held it; the node's next step read the empty
  container. Not a 0.7.10 regression (worldobj.cpp is the same as v0.7.9); A/B on lane 9 below.

## Fix
`wobj.bundle_civ_carried` (worldobj.cpp carried_left_alone): outside Tribe a carried food bundle is left to the game that
carries it: no copy of the host's carried bundle is made on a guest (bundle_make), a guest's own carried bundle counts as
its own (bundle_own: never dropped), and a bound one whose host bundle is gone is not removed (bundle_apply). Ground
bundles are synced as before (BUG-0548). The test hook `coop.wobj bundle take` now takes only a bundle the game still
lists (a delivered one is freed: the first PASS attempt crashed the host on such a pointer, lab 20261108-004405, a test
hook bug, not the fix). New symbol `SporeCoop::Citizen_HoldBundle` 0xC23460 (likely) so crash lines name the faulting
function. `coop.fix wobj.bundle_civ_carried off`: 0.7.10.

## Verification
Lab, lane 10, `civ_village_bundle` (coop_civ_village_bundle.py: civ_npc, the guest's pointer over an old village both
games have; the guest's own creatures carry food; the host's members carry food; the game's object lists of both):
- FAIL (SPORE_FIX_OFF=wobj.bundle_civ_carried): lab 20261108-004130-2 and 20261108-005431 (the final test): the guest's
  own creatures' food removed by the mod within 10 s (kept 0 of 2), the host's carried food put into the hands of 3
  creatures of the guest's own village (and removed again: `carried=3 removed=6`). 0 caught faults in both: the fault
  itself (the gather node holding a removed bundle) is not forced by the test; it shows the removal and the hand-out the
  fault came from.
- PASS: lab 20261108-005021-2, 2 of 2: the guest's own food kept (2 of 2), no hand-out, `left_alone=8`, no fault.
- play_civ on the final build: see History.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-final-triage): play_civ x3 on rel/0.7.10 e7d1734c, lane 9 (lab rel/0.7.10 20261107-151820): 0 faults in
  all three, and no wobj bundle was made on the guest in any of them (the precondition, the host's food bundles next to a
  guest's old village, did not come up), so this A/B cannot tell the versions apart; v0.7.9 not run for the same reason.
  The verdict rests on the code: worldobj.cpp is identical in v0.7.9. #3 failed "no problems (1)" on another class (a
  host vehicle purchase that made no vehicle, the BUG-0325 family).
- 2026-11-08 (fix/w46-k7-findings): fixed (wobj.bundle_civ_carried); civ_village_bundle FAIL x2 / PASS x2.
- 2026-11-08 (fix/w46-k7-findings): play_civ on the final build, lane 9, lab 20261108-010321-2: OK (0 guest faults). (An earlier play_civ, lab 20261108-005631, failed because a campaign was started on the same lane by mistake: not a result.)
