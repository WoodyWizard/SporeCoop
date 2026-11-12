---
id: BUG-0965
title: Creature: a walk-and-use that came past the partner's creature stops beside it 1.4-2 m from the animal without a bite
status: duplicate
kind: bug
stage: Creature
reported: 2026-11-12
source: fix/w48-pass lab 2026-11-12 (partner_pass)
role: guest
versions: 
fixed_in:
branch: fix/w48-walk
commits:
tests:
signature:
docs: docs/re/creature.md#creature-partner-pass-rel, docs/re/creature.md#creature-walk-held-in-mound
related: BUG-0913, BUG-0964
---

## Report
With creature.partner_pass on, the guest's walk-and-use from behind the host's creature (1-1.6 m from the animal) came
to gap 1.1-1.3 m (the bite reaches 1.0), then stood at 1.4-2.0 m beside the host's creature for 12 s without a bite
(the animal's health in the host's game unchanged), further presses doing nothing (lab 20261112-114736 r10,
-122259 r10; about 1 round in 15).

## Root cause
The same as BUG-0964 (a duplicate): the test's `coop.npc gopos` put the guest's creature at the terrain's height next
to the herd's nest, under the nest mound; walking toward the animal on the mound it ran into the mound's collision from
inside and Havok held it in every direction. Not a remainder of BUG-0913 (the partner in the way in another form).

Evidence (fix/w48-walk, lab lanes 2 / 3, docs/re/creature.md#creature-walk-held-in-mound):
* A walk that started and then stood still is the same hold: with `SPORE_FIX_OFF=tools.gopos_surface` partner_pass r9
  (the herd at the nest of radius 506.25, the herd of the old BUG-0965 rounds r10 too) walked 0.5 m and stood, 2.0 m
  from the nest's centre, its radius 0.65 m under the game's surface there, and a plain walk 2 m to the side and 2 m
  back (away from the animal and the partner, the game's WalkTo) moved it 0.0 m: "held in every direction" (lab
  20261112-145911; lane 3 -145911-2: 0.37 m over the terrain, 0.5 m under the surface). A creature stopped by the
  partner's (or any) creature in the way can walk to the side or back. walk_start's solo probes (no session, no
  partner at all, every fix off) showed the same stops after 0.2-1.3 m of walk (lab 20261112-140436, -141120).
* The partner: the BUG-0913 hooks refused every avatar-puppet pair (`pairs_skipped`), no phantom pair between them
  (`phantom_pairs=` .../0/..., fix/w48-pass), the steering of a held avatar has one force (0xB499D0, the static
  obstacles; creatures are not in cObstacleManager), the target stays the animal, so neither Havok, the steering, the
  hover nor the target holds it at the partner.
* With `tools.gopos_surface` on (final code, the median of five surface samples): every walk of partner_pass x4 reached
  the animal (13 of 13 judged, no walk stopped, lab 20261112-153816), walk_start 20261112-154110 (2 runs) none held.
  An earlier version that skipped the lift near another creature still left one guest inside the mound
  (20261112-152630: held in every direction 1.7 m from an animal): that is why the median.
Not re-captured: one of the original "1.4-2.0 m beside the partner" stops itself with the new diagnostics (about 1 round
in 15); the fix-off runs gave holds of the same kind in the same herd.

## Fix
None of its own: BUG-0964's `tools.gopos_surface` (the test's teleport onto the game's surface). partner_pass prints
the game's view of a stopped walk (locomotion request, steering, nearest nest, terrain and surface radius) and a plain
walk to the side and back, so a stop at the partner (free to move) and a hold (held in every direction) are told apart.

## Verification
* no-fail-run: closed as a duplicate of BUG-0964; its FAIL / PASS runs are there (walk_start: FAIL 20261112-143501,
  PASS -143501-2, -143856). Held walks with the switch off in partner_pass: 20261112-145911, -145911-2.

## History
- 2026-11-12: opened.
- 2026-11-12 (fix/w48-walk): the same cause as BUG-0964 (the test's teleport under a nest mound), not the partner:
  closed as its duplicate.
