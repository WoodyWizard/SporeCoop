---
id: BUG-0634
title: Cell: the guest's cell swims through other cells (not a ghost: it still bites and is bitten)
status: open
kind: bug
stage: Cell
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-01
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: cell_new_game guest steers into NPC mirrors and the puppet; count the own collision pairs with mirrors
     (`coop.pool collide`) for soft and bare mirrors. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
