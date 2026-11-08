---
id: BUG-0638
title: Creature: a guest still in its own Cell editor when the host enters Creature plays its creature from another world after the load
status: fixed
kind: bug
stage: Creature
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-04
role: guest
versions: 0.7.8
fixed_in: 0.7.10
branch: fix/w46-creature-p0
commits: 89545851, ccf7cf63, b418a0ab
tests: choice_world (coop_choice_world.py)
signature:
docs: docs/re/creations.md#creations-choice-world
related: BUG-0345, BUG-0344, BUG-0315, BUG-0516, BUG-0508
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/M0ng0r-04 (the guest): "the host went from cell to creature
stage, I was in the editor, after I left the editor I became a species from another world".

## Root cause
Observed (the guest's log): s 652 the guest opens its own Cell editor; s 701-703 the host enters Creature and the vote
ends while the guest is still in it; the world arrives at s 714; back from the editor the guest has only a new own
cell ("own cell 291826DF recorded for world 742FEAAF (BUG-0345)"). s 724-727 after the load: `own=29158399 'Brogity'
from choices.ini ... the game has 29182571 (DIFFERENT)`, "own creature 29158399 recorded for world 742FEAAF
(BUG-0345)", "M0ng0r plays its own creature 'Brogity'".
Observed (code): creations.cpp since the BUG-0345 fix: "a choice in the co-op window holds everywhere"; the guest had an
explicit choice in choices.ini, so it got that old creature in a new world. It never made a creature in this world:
it missed the host's Cell -> Creature editor.
Inferred: not a regression of BUG-0345's code but a gap of its design. Proposed: a guest's explicit choice holds only in
the world it was made in (the host's rule since BUG-0344), or the guest still in its own editor is offered the host's
Cell -> Creature editor.
Side finding (s 813-823): the host's puppet on the guest switched species every ~2 s and "the world's player species"
flipped with it (BUG-0508 lines) ~8 times in 10 s (probably the host trying creatures in the co-op window).

## Fix
`creations.choice_world` (creations.cpp): a guest's explicit choice of a creation (choices.ini) holds only in a world
whose worlds.ini record is that creation; a choice is recorded for the world it is made in (as playing it records it,
BUG-0345). In another world the guest gets its default (this world's record, the host's evolved species, the world's
species). `coop.fix creations.choice_world off`: a choice holds in every world (0.7.9).

## Verification
coop_choice_world.py (suite choice_world, Cell fixture): the guest chooses its newest own creature, opens its own Cell
editor, the host enters Creature and passes its editor; for 45 s after the guest's load both games are sampled every
3 s: the guest's avatar species in its game and the guest's puppet species in the host's game must be the world's
species (a sample with no avatar, during a second world load of the guest, counts as no creature, not another).
* FAIL with `SPORE_FIX_OFF=creations.choice_world`: out/lab/20261106-230447 (older 15 s check), out/lab/20261107-080727
  (the guest swapped to the other world's 290225CD 24 s after the load, the host showed it with that species too).
* PASS: out/lab/20261106-230452 (older check), out/lab/20261107-075854, out/lab/20261107-080319.
* out/lab/20261107-075233 failed on the test itself (two samples with no avatar while the guest loaded the world a
  second time, s 106-112 of its log: the host's world arrived again right after the first load); the test was fixed.
* Not verified: the other proposal (offer the guest still in its own editor the host's Cell -> Creature editor) was not
  done; such a guest plays the world's species until its next editor.

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07 (fix/w46-creature-p0, P0-Creature of 0.7.10): fixed (creations.choice_world); FAIL 20261107-080727, PASS 20261107-075854, 20261107-080319.
- 2026-11-07: released in 0.7.10.
