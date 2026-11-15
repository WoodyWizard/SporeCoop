---
id: BUG-0634
title: Cell: the guest's cell swims through other cells (not a ghost: it still bites and is bitten)
status: not-reproduced
kind: bug
stage: Cell
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-01
role: guest
versions: 0.7.8
fixed_in:
branch: fix/w46-cell-p0
commits:
tests: cell_push (coop_cell_push.py)
signature:
docs: docs/re/cell.md#cell-push
related: BUG-0342
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/M0ng0r-01 (the guest, mark at s 233.5): "I'm not a ghost in cell
stage but I can go into other cells". The guest's shot at s 212 shows its cell overlapping another pink cell and the
host's puppet.

## Root cause
Observed (the guest's log): no NaN / ghost / spare / bare lines; the guest bites, eats and is bitten all session
(bites_sent, eats_sent > 0), so not BUG-0342's ghost. Both players' cells use the key 2912DEEB (the host's own and "the
world's own" on the guest), while the guest's log says it plays its own cell; worth a look of its own.
Inferred: the collision pair with a partner's puppet is dropped by design (h_Pair), so swimming through the host's
puppet is expected; through NPC mirrors it may be mirrors without a soft body (`cell.spare` / `cell.bare_still`,
docs/re/cell.md) that push weakly or not at all. Not verified.

## Fix
None. The guest's cell is pushed by the NPC cells (the host's mirrors) as the host's own cell is by its own NPCs
(Verification). What it swims through is the partner's cell: a pair with a partner's puppet is dropped on both
games by design (h_Pair, docs/re/cell.md#cell-push: the partner's cell is simulated on its owner's machine; a
collision there would bite, eat and push it twice). In the report both players played the same cell (2912DEEB:
the guest used "the world's own"), so the host's cell looked like one more NPC of the guest's own species (the
game populates NPCs of the player's species, docs/re/cell.md#cell-own-species). Making partners solid to each other
would be a new feature (a push sent to the owner), not this report's bug.

## Verification
* Lab `coop_cell_push.py` (each player in turn swims its own cell into an NPC creature of about its size,
  `coop.pool swimto` held for 3 s, the closest centre distance against the two sizes): out/lab/20261106-222954,
  6 pushes each: guest median 0.67, host 0.65 of the two sizes (guest 0.50-1.25, host 0.45-1.36), 46-114 own
  collision pairs per push on both. The guest's mirrors with soft bodies (particles 4/4) and the host's NPCs push
  alike. Earlier probe runs found too few creatures of the size (20261106-215829, -220209: 1 push each, guest 0.40,
  host 0.91).
* Not a ghost: the report's guest bit and was bitten all session (BUG-0342's ghost ruled out in the intake).
## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-06: not reproduced in fix/w46-cell-p0: the guest is pushed by NPC mirrors as the host by its NPCs (lab 20261106-222954); the partner's cell is passed through by design.
