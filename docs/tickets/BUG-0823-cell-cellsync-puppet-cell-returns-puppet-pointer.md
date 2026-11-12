---
id: BUG-0823
title: Cell: cellsync::puppet_cell() returns puppet pointers without the pool index check (a new avatar at an old puppet's address) (suspected)
status: not-reproduced
kind: bug
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch: fix/w48-freed
commits: 1e58b7e0, be40ef44
tests: cell_puppet_cell, cell_puppet_cell_long
signature:
docs: docs/06-testing.md#testing-freed-block-reuse
related: BUG-0342 BUG-0221 BUG-0426
---

## Report
No player report. In the frame of a level rebuild (the host's death without a helper, an own-cell apply) the host's own cell may be used as a guest's attacker or get the knockout look. Suspected.

## Root cause
Checked at runtime (fix/w48-freed, 2026-11-12): the host's level rebuild while the guest is knocked out (the host's death without a helper) put the host's new own cell at the guest's puppet's old address in 1 of about 16 rebuilds (lab 20261112-012332 round 4, with the guard off); its opacity stayed 1.0 in the 0.2 s samples (the window is one frame). The mod's own lookup never handed the host's own cell out as the partner's in 8 more rebuilds with the guard off (`handed_own=0`, lab 20261112-012832, 20261112-013239; no coincidence there). The code path the ticket names is real (puppet_cell before the remote loop's drop), the effect was not seen.

## Fix
Kept as hardening (as BUG-0342's puppet check): mods/coop/cellsync.cpp puppet_cell returns a partner's cell only while modes::cell_is_puppet (its pool index) says so. Switch: `coop.fix cell.puppet_cell_index off`. `coop.pool selfstate` prints the own cell, its opacity and `handed_own=` (the lookup handed the own cell out).

## Fix idea
puppet_cell returns p.cell only when modes::cell_is_puppet(p.cell).

## How to test
Three players: knock one guest out, host `coop.pool selfhit` (rebuild) while the other guest repeats `coop.pool bite` on a host NPC; assert no bite applied with the host's own cell as attacker and no fault.

## Verification
no-fail-run: not reproducible in the lab: the address coincidence came once in about 16 rebuilds and left nothing visible; the tests `cell_puppet_cell` / `cell_puppet_cell_long` pass with the guard off (out/lab/20261112-012332, 20261112-012832, 20261112-013239) and on (out/lab/20261112-012019, 20261112-013003).

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12: not reproduced on fix/w48-freed (the guard kept, cell.puppet_cell_index).
