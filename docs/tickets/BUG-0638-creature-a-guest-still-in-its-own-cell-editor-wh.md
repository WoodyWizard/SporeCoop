---
id: BUG-0638
title: Creature: a guest still in its own Cell editor when the host enters Creature plays its creature from another world after the load
status: open
kind: bug
stage: Creature
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-04
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: host and guest in Cell, the guest has an explicit choices.ini creature from another world; the guest opens
     its own Cell editor, the host evolves to Creature meanwhile; assert the guest's avatar species after the load
     (the world's species or a creature it made in this world). -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
