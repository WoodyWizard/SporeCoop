---
id: BUG-0716
title: Cell: a guest's poison part did nothing (its own game destroyed its clouds at once, the host's game made none); a mirror's other attackers
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
tests: cell_poison
signature:
docs: docs/re/cell.md#cell-electric-attack
related: BUG-0428, BUG-0714, BUG-0715
---

## Report
From the BUG-0428 audit (K2b, 2026-11-08): on a guest, damage to a mirror counts only when the guest's avatar is the attacker (h_DamageCell); the other attackers are dropped (low impact, decide). Which attackers are there, and does anything the guest's creature does get lost that way?

## Root cause
The other attackers of a mirror on a guest: another mirror (the host simulates the same pair: dropping is right), a partner's puppet (its owner's game sends its own hits: right), and the cells the guest's game makes for its avatar: the poison clouds (0xE76660, +0x110 set). Observed (runtime, lane 6): the guest's visibility step 0xE78C00 destroyed every cloud of its avatar in the frame it was made (return address 0xE78C37: a poison cell outside the active area, and a guest has no active tiles of its own; 101 of 101, lab 20261108-042625); kept alive for a test they met no mirror in 2 minutes (lab 20261108-043126); and on the host the guest's cell is a puppet without AI, so no poison step ran for it (puppet_clouds 0, lab 20261108-044437). So a guest's poison part made nothing anywhere.

## Fix
`cell.puppet_poison` (cellsync.cpp `puppet_poison`, host, once per cell step): the host runs the game's own poison step (SporeCoop::Cell_PoisonStep 0xE76880, ESI = the cell) for every partner's puppet with the puppet lent the avatar index for the call (as the NoAttack hook does: the cloud gets the avatar's resource and +0x110, a player's). The clouds are made by the host's game from the partner's creation, act on the host's creatures there and come to the guests as mirrors. The guest-side drops stay (counted: `foreign_hits=<mirror>/<puppet>/<other>`). Off: no poison step for puppets.

## Verification
Test `cell_poison` (tools/tests/coop_cell_parts_attack.py --part poison: the guest's creation given a poison part in the host's game, `coop.pool borrow poison graft p<id>`, the guest next to a host creature).
* FAIL with `SPORE_FIX_OFF=cell.puppet_poison`: out/lab/20261108-044437 (the host's game made no cloud of the guest, puppet_clouds=0)
* PASS: out/lab/20261108-044613 (67 clouds of the guest), out/lab/20261108-044751 (72)
* Not verified: the clouds' damage. With the values written into the model's data the creature next to them lost 2 hp in one run and none in the other; the host's own avatar with the same model data made 90-162 clouds and hurt nothing either: what a real poison part carries besides model data +0x1270 / +0x1274 / +0x1278 is not read (docs/re/cell.md#cell-electric-attack). The damage itself is the host's game's own.
Re-run on the merged branch fix/w46-k2b-content (the run dirs above were in the sub-worktrees fix/w46-k2b-content-a / -b, removed 2026-11-08; their ids stay as the record): FAIL out/lab/20261108-065555, PASS out/lab/20261108-054842.

## History
- 2026-11-08: opened.
- 2026-11-08: fix/w46-k2b-content-a: the guest's clouds traced (destroyed at once on the guest, none on the host), fixed host-side (cell.puppet_poison), test cell_poison; the damage of the grafted part not reproduced.
- 2026-11-08: FAIL / PASS runs repeated on the merged K2b branch.
- 2026-11-08: released in 0.7.11.
- 2026-11-10 (plan/w47-closed-fixed): fixed-ticket review: the poison test asserts the mod's cloud counter (coop_cell_parts_attack.py:108), not damage in the host's game; the damage gap is new ticket BUG-0892.
