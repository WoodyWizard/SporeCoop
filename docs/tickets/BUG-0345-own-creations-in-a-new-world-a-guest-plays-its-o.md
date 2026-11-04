---
id: BUG-0345
title: Own creations: in a new world a guest plays its own cell or creature from another world; the host reads it as a bug
status: fixed
kind: bug
stage: Cell
reported: 2026-10-31
source: player reports 2026-10-31 (host marks)
role: host
versions: 0.7.3.1
fixed_in: 0.7.5
branch: fix/w41-cell-left
commits: 4b9a0875 dd8537e9 dc920646 28ec9f25
tests: creations (tools/tests/coop_creations.py)
signature:
docs: docs/re/creations.md
related: BUG-0342, BUG-0344
---

## Report
Host marks in two sessions (0.7.3.1, both in Cell right after the guest joined a new world):
"New world, guest got his own cell from another world ..." and "Guest has creature from another
world." (both typed "quest", the context makes clear the guest is meant: no quest card was open,
the stage was Cell, and the guest's cell was the guest's own creation).

## Root cause
Observed: by design (docs/re/creations.md, `creations.cpp`) a guest plays its own cell or
creature: its choices.ini choice or its newest own creation from My Creations, also in a world it
has never played. In both sessions the guest's own cell from its own library replaced the world's
cell at the load (`own cell is now ... (level rebuilt)`), and the host saw a cell that does not
belong to the new world.

Observed (code, 2026-11-04, fix/w41-cell-left): creations::mine() gives a guest without a choice the newest own
creation of its My Creations, in any world (the previous game's cell is that one); the host has played the
world's species since BUG-0344. The DNA: the Cell parts states are the world's, not the creation's, CollectPart
adds no food, and the guest's DNA jump on joining is the host's shared total; the host's +68 were meals
(BUG-0510). No DNA from an own creation's parts (docs/re/creations.md#creations-guest-other-world).
Inferred: players read the feature as a bug when nothing tells them. In the first session it came
together with the ghost (BUG-0342), which made it look broken.

## Fix
Decision (orchestrator, owner delegated, 2026-11-04): option (b) per world plus (a). `creations.guest_world_species`
(creations.cpp mine / own_world_tick / adopt_after_editor): a guest plays its own creation by default only when it
chose it or already played it in this world (SporeCoop\worlds.ini per kind and world crc; an own editor's result is
recorded for the world, not as a choice); else the world's species. A notice on every game "<name> plays its own
creature / cell". Off: the newest own creation in any world (0.7.4). docs/re/creations.md#creations-guest-other-world.

## Verification
FAIL: SPORE_FIX_OFF=creations.guest_world_species creations, lab out/lab/20261104-033933 (nothing chosen in a new
world: the guest plays its newest own creature 290225CD; no record, no notice).
PASS: creations, lab out/lab/20261104-033829 (world's species 065CBE70 with nothing chosen; chosen -> own, notice on
both games, recorded; unchosen in the same world -> still own; record forgotten -> world's species; the window's next).

## History
- 2026-10-31: opened from the host's report zips (two marks).
- 2026-11-03: reported again (0.7.3.6 direct message: the guest spawns in Cell with its previous cell creature; older message: the guest gets a monster from a different playthrough). Guest log (report 20261104-005925): `own=29122D00 from newest own creation ... the game has E219E5D3 (DIFFERENT)` then swapped to its own; in Creature `own=29149794 from choices.ini`. Players read it as a bug; the DNA half is BUG-0510.
- 2026-11-04: fix/w41-cell-left: where the choice is made and the DNA question answered (no DNA from parts); the design decision (a/b/c) left to the owner.
- 2026-11-04: fix/w41-cell-left: the per-world default and the notice (creations.guest_world_species); fixed (unreleased).
- 2026-11-04: released in 0.7.5.
