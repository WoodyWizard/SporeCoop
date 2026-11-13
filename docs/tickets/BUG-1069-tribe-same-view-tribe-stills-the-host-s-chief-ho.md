---
id: BUG-1069
title: Tribe: same_view_tribe stills: the host's chief holds its staff in the hand, the guest's copy has it in the resting grip (view 4) or held another way (view 1)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-13
source: fix/w49-mirror final check, lab 20261113-051820-2 same_view_tribe
role: guest
versions: 
fixed_in: unreleased
branch: fix/w49-tribe3
commits: 9051b80c 3756afed 6d72a5d7
tests: tribe_chief_still, same_view_tribe (the chief rows at each still)
signature:
docs: docs/re/tribe.md#tribe-chief-load-gesture
related: BUG-0962, BUG-0988, BUG-0968, BUG-0992
---

## Report
Key frames of fix/w49-mirror's final check (out/lab/20261113-051820-2, sheets 005 / 008 same_view_tribe view 1 / 4),
after BUG-0972 (a returned gesture) and BUG-0988 (the animation's moment) were fixed: view 4: the host's chief ("Alice's
chief") holds its staff in the hand, slanted forward; the guest's copy has it upright in the resting grip. View 1: the
staff upright in the hand on the host, slanted down with the blade forward on the guest. The tests of the hold
(tribe_hold_guest x2) and the phase (tribe_mirror_phase) passed in the same runs, so this is a moment the hold relay
(BUG-0914 / 0971 / 0968 / 0992) or the item's grip differs at the still, not the animation's phase. Not analysed.
Reproduce: `lab.py run same_view_tribe --repeat 3` and compare the chief's staff in the stills; sample `coop.tribe anims`
(`hold=`, `t=`) of the chief on both games at the still's moment.

## Root cause
Not the hold relay alone: the gesture's moment, in three ways (observed, docs/re/tribe.md#tribe-chief-load-gesture).
* same_view_tribe with the chief's rows at each still (lab 20261113-060215 #6; the stills of #3 showed the staff
  forward on the host, upright in the grip on the guest): the host's chief stands in its staff gesture 0x04C6CB02
  begun at the save's load, `bhv=00000000` (no behaviour start seen), t 16.7-22.2 s; the mirror in the same id at t
  0.6-6.1 s (its own start), hold 0 at the first view. The moment relay (kind 14, BUG-0988) only covered animations
  equal to the behaviour's last start, so none was sent for this one.
* tribe_chief_still (lab 20261113-060910): the same gesture 5.6 s apart after the host restarted it; the phase fix
  ignored differences of 5 s and more. The mirror's older instance ended 1.9 s before the host's (hold `M`, the hand
  empty 1.4 s on the guest).
* Same run: the host restarted the gesture with a new hold: holdtrace `S T R` at one moment, the relayed restart ended
  the instance the hold had just bound to; the guest's hand empty 1.1 s until the hold's resend.

## Fix
* `tribe.mirror_phase_any`: the host sends the moment of every member's current animation that is neither a loop nor
  the locomotion's idle / walk (MSG_NPC_ANIM kind 15, kind 14's layout; a guest with the switch off ignores it, so the
  twin acts from the join); a guest sets differences up to 60 s (5 s before).
* `tribe.hold_restart`: a relayed restart of the animation a host's hold was bound to under 0.5 s before is skipped
  while the mirror plays it (trace `D`); the moment comes with kind 14 / 15.
* Tests: tribe_chief_still (new); same_view_tribe prints the host's chief's animation / hold / time / item on both
  games at each still.

## Verification
tribe_chief_still: FAIL 20261113-060910 (lane 7, the build before the fix: hold differs 1.4 s and 1.1 s, the same
gesture 5.6 s apart for 4.1 s), FAIL 20261113-062027 (lane 6, SPORE_FIX_OFF=tribe.mirror_phase_any,tribe.hold_restart:
the gesture 5.4-14 s apart in 4 stretches, 2-10 s long); PASS 20261113-062025 (lane 7), PASS 20261113-062735 (lane 7).

## History
- 2026-11-13: opened.
- 2026-11-13: cause found and fixed on fix/w49-tribe3.
