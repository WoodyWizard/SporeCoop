---
id: BUG-0759
title: Tribe: sync-check t: 1 apart (t4 11.5) in the 4-player campaign: the host's chief member moves at 120 m/s while the guest's mirror stands
status: open
kind: desync
stage: Tribe
reported: 2026-11-08
source: lab 20261108-114435 campaign_tc2 (rel/0.7.11 75365a87, lane 5)
role: guest
versions: 0.7.10, 0.7.11
fixed_in:
branch:
commits:
tests:
signature: sync-check: MISMATCH with the host (Tribe A3930F03, 52 objects compared): t: 1 apart (t4 11.5); host `t4 here: ... speed=119.7 host_speed=0.0 ... tool=11 leader=1`, guest `... speed=0.0 host_speed=119.7 bt=FFFFFFFF ours=1 tool=11 leader=1`
docs:
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
Not analysed. Inferred: 120 m/s is a jump of the host's member within one or two frames (~4 m a frame at 30 fps),
not a walk; the game moves a chief so (a snap after a fall or a regroup, an order's arrival), and the mirror's driving
(drive.lead_keep / drive.lead_fall, BUG-0410 / BUG-0674) follows a walk, not a jump: the guest's mirror stays until the
next states move it. BUG-0410/0674's tests cover walks and turns, not a jump of a member.

## Fix
Not done. Next step: coop.bodyrec rows of the host's chief in campaign_tc2 (SPORE_BODYREC_DIAG=1) to see what moves it at
120 m/s and how long the mirror stays off; then either snap a mirror over a jump (as NPC proxies snap) or let the
sync-check take a jump of the last second as no difference.

## Verification
Open. campaign_tc2 (40 min, rare: once per run at most).

## History
- 2026-11-08: opened.
