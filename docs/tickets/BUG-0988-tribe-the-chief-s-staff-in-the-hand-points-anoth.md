---
id: BUG-0988
title: Tribe: the chief's staff in the hand points another way on the guest's mirror than on the host (tribe_mirror_hold hold, same_view_tribe view 4)
status: open
kind: desync
stage: Tribe
reported: 2026-11-12
source: 0.7.13 final check key frames, lab 20261112-175447
role: guest
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
Key frames of the 0.7.13 final check (out/lab/20261112-175447, rel/0.7.13): 063_tribe_mirror_hold_02_hold: the host's
chief holds its staff in the hand (both games report the hold, `hold=1`, in the idle 0x04866DAB): on the host the staff
stands vertical above its head, on the guest the same staff lies diagonal across the body, the blade forward-down.
057_same_view_tribe_04_view4: the staff tilts down-left on the host, almost vertical on the guest. 062 (swing): Bob's
chief leans forward on the host and stands upright facing the camera on the guest. Same item, same animation id on
both games: the pose within the animation differs.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Inferred, not proven (fix/w48-ftribe, 2026-11-12): the mirror plays the same animation (tribe_mirror_idle: 734 of 739
samples the same id) but not at the same phase: the relay starts an animation when its start reaches the guest (one
network delay later) and a looping idle on the mirror is the mirror's own loop (started by its own idle refresh or
locomotion at its own time); the hold (the started modifier that puts the item into the hand) is bound to the
animation playing, so the arm and staff follow the mirror's phase. No phase or time of an animation is relayed today
(MSG_NPC_ANIM carries ids only). The tests compare the id and the hold, both equal here.
A fix needs the animation's time (GetCurrentAnimation's float* time out-argument, AnimatedCreature vt 0x58) sent with the
start / snapshot and set on the mirror (a setter not known yet): RE and a new message field, not for 0.7.13. Player
impact: an arm or staff angle at one moment; the item, the animation and the hold match.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
- 2026-11-12: fix/w48-ftribe: seen in key frames 057 / 062 / 063 of the 0.7.13 final check; left for a later release (animation phase, RE needed).
