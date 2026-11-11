---
id: BUG-0823
title: Cell: cellsync::puppet_cell() returns puppet pointers without the pool index check (a new avatar at an old puppet's address) (suspected)
status: open
kind: bug
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0342 BUG-0221 BUG-0426
---

## Report
No player report. In the frame of a level rebuild (the host's death without a helper, an own-cell apply) the host's own cell may be used as a guest's attacker or get the knockout look. Suspected.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): cellsync.cpp:449 puppet_cell returns p.cell from C.puppets() (coop.cpp:6814) without modes::cell_is_puppet / puppet_now; used by puppet_poison (writes the avatar index from F<uint32_t>(pc,0)), the host's guest_attack (o_Bite / o_DamageCell with the puppet as attacker), on_ko and the knockout opacity. cellsync::tick and the message dispatch run before the remote loop that drops dead puppets (coop.cpp:908); RebuildLevel puts the new avatar at an old puppet's address (BUG-0342). Confidence: low-medium.

## Fix
Not fixed yet.

## Fix idea
puppet_cell returns p.cell only when modes::cell_is_puppet(p.cell).

## How to test
Three players: knock one guest out, host `coop.pool selfhit` (rebuild) while the other guest repeats `coop.pool bite` on a host NPC; assert no bite applied with the host's own cell as attacker and no fault.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
