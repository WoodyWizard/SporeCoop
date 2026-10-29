---
id: BUG-0001
title: Cell: enemies spin on the guest and the guest cannot finish them
status: fixed
kind: desync
stage: Cell
reported: 2026-10-24
source: player report (Reddit, two brothers)
role: guest
versions: 0.7.x
fixed_in: 0.7.2.2
branch: fix/cell-spin
commits: 76deaa4, a87beae
tests: coop_cell_spin
signature: 
docs: docs/re/cell.md 10
related:
switch: pool.mirror
---

## Report
On the guest the host's cells kept turning between the two players, so neither could hit them; the guest could wound a creature but never kill it.

## Root cause
SoftBodyStep 0xE5A430 recomputes a mirror's facing from its soft-body particles every step, overwriting the host's heading. Lethal bites (BreakOrSwallow) and mouth chunk bites (EatChunkA/B) bypass DamageCell, so the guest's killing bites never reached the host.

## Fix
cellsync.cpp: h_Soft turns the mirror's particles to the host heading after SoftBodyStep; h_Break and h_ChunkA/B forward the guest's bites as MSG_CELL_HIT.

## Verification
coop_cell_spin.py fails on the old code, 3/3 with the fix (facing error 150 deg -> 3-6 deg).

## History
- 2026-10-24: reported (player report (Reddit, two brothers)).
- 2026-10-24: fixed on fix/cell-spin, released in 0.7.2.2.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: switch: pool.mirror (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
