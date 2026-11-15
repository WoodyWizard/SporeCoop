---
id: BUG-0759
title: Tribe: sync-check t: 1 apart (t4 11.5) in the 4-player campaign: the host's chief member moves at 120 m/s while the guest's mirror stands
status: not-reproduced
kind: desync
stage: Tribe
reported: 2026-11-08
source: lab 20261108-114435 campaign_tc2 (rel/0.7.11 75365a87, lane 5)
role: guest
versions: 0.7.10, 0.7.11
fixed_in:
branch: fix/w46-b1-tribe
commits:
tests: tribe_mirror_jump
signature: sync-check: MISMATCH with the host (Tribe A3930F03, 52 objects compared): t: 1 apart (t4 11.5); host `t4 here: ... speed=119.7 host_speed=0.0 ... tool=11 leader=1`, guest `... speed=0.0 host_speed=119.7 bt=FFFFFFFF ours=1 tool=11 leader=1`
docs: docs/re/tribe.md#tribe-mirror-jump
related: BUG-0410, BUG-0674, BUG-0685
---

## Report
0.7.11 final check (lab 20261108-114435, campaign_tc2: the campaign's Tribe stage to Civ, 4 players, lane 5): the test
printed RESULT: OK, the lab failed it on the sync-check gate, once, at s 1049.8 between the host and the guest (Bob):
`t: 1 apart (t4 11.5)` (campaign_tc2.host.game.log 831-832, campaign_tc2.guest.game.log 1035-1036). t4 is the host
tribe's chief (`leader=1 tool=11`, owner 254, hp 99). On the host it moved at 119.7 m/s at the check (pos 368.8
-155.0 328.2), on the guest its mirror stood (speed 0, bt FFFFFFFF, pos 360.6 -151.5 320.9). The host's line says
"2 checks 3 s apart". Nothing else in either log around it (no KO, no leader change, no load).
The same gate shape (`t: 1 apart`, one member ~10 m) in campaign_tc2 on 0.7.10 (fix/w46-tribe-leader lab
20261107-163249: `t58 10.7`, a plain member walking 1.5-2.5 m/s), noted in BUG-0685 as the BUG-0410/0674 class
without a ticket of its own. Not a 0.7.11 regression (seen on 0.7.10 code).

## Root cause
Not found. Observed (campaign_tc2 logs of lab 20261108-114435): t4 is member #4 (owner 254, the tribe's leader, tool 11,
hp 99) in the sleep node 0x04611FF4 on the host with its velocity field at 119.7 m/s at both checks 3 s apart and its
position 11.5 m from the guest's mirror (speed 0, no tree). Observed (Ghidra, GOG 2024): the sleep tick 0xDAF7B0 walks
the member into its hut's radius (vt 0x60) and stops it (vt 0xEC); it teleports nothing.
Observed (runtime, lane 7, fix/w46-b1-tribe; every run below passed the position check, i.e. the mirror followed):
* 20261108-145456: the host's chief put 10 m away (`coop.tribe put`, a teleport): the guest's mirror within 0.3 m in
  0.4 s; the chief ordered to sleep (CitizenAction 6) at full health: 5 m jump out of the hut (128.7 m/s in one state),
  the mirror 0.8 m off 0.5 s later.
* 20261108-145905: the chief at 30 hp (`coop.tribe sethp`) ordered to sleep: node 0x04611FF4 for 22 s, a 5 m jump on the
  way in, the mirror 0.7 m off the whole sleep.
* 20261108-150839: an NPC tribe's chief (owner 254, as t4) at 20 hp ordered to sleep at its hut, then its velocity held
  at 113 m/s for 8 s (`coop.tribe setvel`): the mirror 0.0-0.1 m off.
* tribe_mirror_jump (a member put 11.5 m away and its velocity held at 120 m/s for 6 s, the state of the report):
  20261108-150405 (velocity only: 0.7 m), 20261108-150538 #1, #2 (0.0 m), 20261108-150728 with a 100 ms delay on the
  guest's link (0.2 m).
A fix tried on the way (`tribe.mirror_jump`: a state over 30 m/s taken as standing) was removed: its off side never
failed. Inferred: the campaign's state needs something these do not have (4 players, the guest's own mirror held by
its game in the hut's collision, or the chief out of the guest's view).

## Fix
None (not reproduced). Test tools: `coop.tribe sethp <id> <hp>` and `coop.tribe setvel <id> <vx> <vy> <vz> <s>` (host),
the regression test tribe_mirror_jump (no twin).

## Verification
no-fail-run: not reproduced with a targeted 2-player recipe in 7 runs (above); the regression test tribe_mirror_jump
passes (20261108-150538 #1, #2, 20261108-150728). The campaign case stays in docs/known-issues.md#issue-tribe-chief-jump
with the recipe tried.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-b1-tribe): targeted repros (teleport, sleep in the hut, an NPC chief, a velocity held at 120 m/s, a 100 ms link) all followed by the mirror; closed not-reproduced, a regression test added.
