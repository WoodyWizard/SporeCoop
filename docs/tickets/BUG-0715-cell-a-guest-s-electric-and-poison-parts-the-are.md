---
id: BUG-0715
title: Cell: a guest's electric part never hurt the host's creatures (the chain attack 0xE71520 writes the hp itself)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-08
source: BUG-0428 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2b-content-a
commits: e81a56d9 ad15c36c aaadb06c
tests: cell_electric
signature:
docs: docs/re/cell.md#cell-electric-attack
related: BUG-0428, BUG-0714, BUG-0716
---

## Report
From the BUG-0428 audit (K2b, 2026-11-08): the electric / poison area attack 0xE71520 writes hp without DamageCell (docs contradicted: section 6 named DamageCell type 3 as periodic poison/electric). Needed a runtime check: does a guest's electric part hurt a host creature?

## Root cause
Static (Ghidra, GOG 2024): 0xE71520 (SporeCoop::Cell_ElectricAttack, called only by the electric step 0xE71980) is the electric part's chain attack and writes each target's hp itself (`hp -= GetDamageAmount`), no DamageCell; poison is a separate step (0xE76880, clouds: BUG-0716). The docs' "DamageCell type 3 periodic poison/electric" is not this path. Observed (runtime, lane 6, lab 20261108-035148, the fix off): a guest's avatar with an electric part (its model data given one: the fixture's pool has no electric model) zapped 12 times next to a host creature with hp 60: the mirrors lost hp in the guest's game only, the host's creature kept 60 (no message).

## Fix
`cell.electric` (cellsync.cpp h_Zap, hooked on SporeCoop::Cell_ElectricAttack): on a guest, when the avatar attacks, the mirrors' hp before the chain is put back after it (no local death through event 2's KillIfDead) and each mirror's loss goes to the host as MSG_CELL_HIT type 1 (DamageCell's animation for type 1 is the chain's 0xE), which the host applies through DamageCell with the guest's puppet (BUG-0714). A knocked-out avatar zaps nobody (BUG-0509). `coop.pool` (guest) `zaps= zap_hits=`; `coop.pool borrow electric|poison|off [graft]` (tests). Off: the chain stays local.

## Verification
Test `cell_electric` (tools/tests/coop_cell_parts_attack.py --part electric).
* FAIL with `SPORE_FIX_OFF=cell.electric`: out/lab/20261108-035148 (12 zaps on the guest, no type-1 hit on the host, the creature kept hp 60)
* PASS: out/lab/20261108-034904 (4 type-1 hits applied, hp 57 -> 51), out/lab/20261108-035327 (3 type-1 hits, 57 -> 51), out/lab/20261108-052922 (the final code)
Re-run on the merged branch fix/w46-k2b-content (the run dirs above were in the sub-worktrees fix/w46-k2b-content-a / -b, removed 2026-11-08; their ids stay as the record): FAIL out/lab/20261108-065413, PASS out/lab/20261108-054842.

## History
- 2026-11-08: opened.
- 2026-11-08: fix/w46-k2b-content-a: the runtime check (bug confirmed), fixed (cell.electric), test cell_electric.
- 2026-11-08: FAIL / PASS runs repeated on the merged K2b branch.
- 2026-11-08: released in 0.7.11.
