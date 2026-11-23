---
id: BUG-9317
title: CELL-3: Cell: the host's copy of a guest's mate can be killed or eaten on the host (repeatable free meat, the mate dies on the host only)
status: open
kind: bug
stage: Cell
reported: 2026-11-22
source: audit
role: host
versions: 0.7.16.1
fixed_in:
branch: fix/w63-life
commits: 75d69f4a, 39f2f060
tests: cell_mate_copy (coop_cell_mate_copy.py); twin cell_mate_copy_off
signature:
docs: docs/06-testing.md#testing-w63-life-knobs
related: BUG-1246
---

## Report
External static audit of 0.7.15.5, item CELL-3 (out/research/ext-audit-verify-uifx-cell.md): the host's copy of a guest's mate (BUG-1246) can be killed in the host's game: repeatable free meat (it is made again 0.2 s later) and the guest's mate dies on the host's screen only.

## Root cause
Inferred from code and docs/re/cell.md#cell-guest-hit-game (Ghidra, verified there): the copy was protected by +0x111 alone; Bite's lethal branch (BreakOrSwallow), EatChunk / KillIfDead and a swallow do not ask it. The host's handlers did not know the copy (MSG_CELL_EAT refused only the avatar and puppets, MSG_CELL_HIT the same), the copy goes to the other guests as an ordinary cell, and mate_in makes it again on the next pose after a kill. Every kill dropped the game's loot.

## Fix
`mate_copy_cell` / `mate_copy_refuse` (cellsync_mate.cpp): on the host the copy is a picture like a partner's puppet: no collision pair (h_Pair), no damage (h_DamageCell), no death (h_KillCell, h_Break), no chunk (EatChunk A/B), no swallow (h_Swallow); a guest's MSG_CELL_HIT is refused and its MSG_CELL_EAT finds no cell (an eat: the points taken back as eaten first). Counted as copy_refused in `coop.pool mate`; the copy line shows hp and dying. Switch `coop.fix cell.mate_copy_safe off`: as 0.7.16. Test knob `coop.pool cellhp c<idx> <hp>` (host). No message changed.

## Verification
Open, waiting for the lab run. PASS expected: cell_mate_copy (the guest's mating call; the host sets the copy's hp to 1 and bites it with the game's Bite three times and hits it once: the same copy alive and not dying, not made again, no loot near it, the guest's mate alive). FAIL expected: cell_mate_copy_off (SPORE_FIX_OFF=cell.mate_copy_safe: the copy breaks, drops meat, is made again). Not covered by a lab test: the 3-4 player path (another guest's bite, MSG_CELL_HIT / MSG_CELL_EAT), the same predicate. Independent of all.shared_dna.

## History
- 2026-11-22: opened (audit item); fixed on fix/w63-life with its test and twin, waiting for the lab run.
