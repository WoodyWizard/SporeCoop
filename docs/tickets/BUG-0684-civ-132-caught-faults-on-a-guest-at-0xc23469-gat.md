---
id: BUG-0684
title: Civ: 132 caught faults on a guest at 0xC23469 (gather node, null bundle in 0xC23460) next to an old tribe village, wobj carried bundles
status: open
kind: crash
stage: Civilization
reported: 2026-11-07
source: lab 20261107-141346 play_civ (rel/0.7.10 e7d1734c, lane 3)
role: guest
versions: 0.7.9+
fixed_in:
branch:
commits:
tests:
signature: 0xC0000005 at 0x00C23469 (labelled SporeCoop::Citizen_IsLeader+0x39, really the function 0xC23460) data 0x34, edi 0; calls 00D74D66 00D74DA0 00D74DBD 00B38500 00B384A5 00B212EF (gather node state 0x10 -> 0xC23460(null bundle))
docs: docs/re/tribe.md#tribe-carried-bundles
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
<!-- planned: outside Tribe (no member mirrors) leave a carried bundle of a guest's own creature alone and do not
     hand the host's carried bundles to creatures that are not mirrors -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
