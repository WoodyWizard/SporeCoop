---
id: BUG-0988
title: Tribe: the chief's staff in the hand points another way on the guest's mirror than on the host (tribe_mirror_hold hold, same_view_tribe view 4)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-12
source: 0.7.13 final check key frames, lab 20261112-175447
role: guest
versions: 
fixed_in: 0.7.14
branch: fix/w49-mirror
commits: f3f432ff
tests: tribe_mirror_phase, tribe_mirror_hold, same_view_tribe
signature:
docs: docs/re/tribe.md#tribe-mirror-pose-phase, docs/06-testing.md#testing-t2-mirror
related: BUG-0972, BUG-0992, BUG-1069
switch: tribe.mirror_phase
---

## Report
Key frames of the 0.7.13 final check (out/lab/20261112-175447, rel/0.7.13): 063_tribe_mirror_hold_02_hold: the host's
chief holds its staff in the hand (both games report the hold, `hold=1`, in the idle 0x04866DAB): on the host the staff
stands vertical above its head, on the guest the same staff lies diagonal across the body, the blade forward-down.
057_same_view_tribe_04_view4: the staff tilts down-left on the host, almost vertical on the guest. 062 (swing): Bob's
chief leans forward on the host and stands upright facing the camera on the guest. Same item, same animation id on
both games: the pose within the animation differs.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (tribe_mirror_phase with the fix off: the main track's time +0xA8 on both games): a relayed behaviour
animation runs 0.05-0.16 s behind the host's member (median 0.10 s; 0x04C6CB02 0.16 s), up to 2 s after a late start.
The relay sends ids only; nothing brought the mirror to the host's moment. The returned gesture of BUG-0972 was the
other part of the stills (a mirror in a gesture the host had left).

## Fix
Once a second the host sends each member's time in its behaviour animation (MSG_NPC_ANIM kind 14, not for loops); a
guest whose mirror plays it more than 0.1 s from that time + RTT / 2 writes its track's time there. Switch
`coop.fix tribe.mirror_phase off`.

## Verification
Test `tribe_mirror_phase` (coop_tribe_mirror_phase.py: both games' track times of the same animation, median
difference under 0.08 s, 80 % under 0.15 s).
FAIL (SPORE_FIX_OFF=tribe.mirror_phase): out/lab/20261113-050335-2 (median 0.099 s, 77 %). PASS: out/lab/20261113-050335
(median 0.046 s, 98 %), and the final check runs (see History).

## History
- 2026-11-12: opened.
- 2026-11-12: fix/w48-ftribe: seen in key frames 057 / 062 / 063 of the 0.7.13 final check; left for a later release (animation phase, RE needed).
- 2026-11-13: fixed on fix/w49-mirror (package T2 of 0.7.14).
- 2026-11-13: fix/w49-mirror final check: the phase is the host's (tribe_mirror_phase PASS on both lanes), but the same_view_tribe stills still showed the chief's staff in the hand on one game and in the grip on the other: a hold difference, not the phase; opened BUG-1069.
- 2026-11-13: fix/w49-mirror final check: tribe_mirror_phase PASS 20261113-051820-2 and 20261113-052132.
- 2026-11-14: released in 0.7.14.
