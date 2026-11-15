---
id: BUG-0428
title: class: game rules copied by hand instead of calling the game (umbrella)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2b-content
commits: e81a56d9 ad15c36c aaadb06c
tests: cell_guest_hit, cell_electric, cell_poison, npc_hit_feedback, cell_guest_bite, cell_kill_once, cell_ko_bite
signature:
docs: docs/re/cell.md#cell-guest-hit-game
related: BUG-0714, BUG-0715, BUG-0716, BUG-0717, BUG-0509, BUG-0078, BUG-0240, BUG-0350
---

## Report
Fixed per case: BUG-0078, 0240 (hand-copied DamageCell gate), BUG-0001 bites (hook list).
Siblings not covered: Cell electric / poison area attack 0xE71520 (from 0xE71980 @0xE71BCA) writes hp at 0xE71915 without DamageCell 0xE7A7C0 / KillIfDead: a guest's electric/poison parts never hurt host cells, no meat (obs static); guest lethal bite ACT_BREAK skips the gate (cellsync.cpp ~3312) (inf); damage from a guest's non-avatar attacker dropped (inf); Creature guest hits skip the whole TakeDamage on the guest (npc.cpp:1544-1559): hit feedback lost (BUG-0350 fixed only the Cell ring).
General fix: call the game's own function with the puppet as actor, or forward hp at the write point. Outcome check: every damage type from the guest: host hp and death, guest feedback state.
Source: docs/generalisation-plan.md G12.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
K2b audit (0.7.11): every item was still present: the host applied a guest's bites by hand (no hurt animation, no knockout gate on a lethal bite), the electric chain attack wrote hp itself, a guest's poison clouds were destroyed by its own game. Correction: the game's own lethal bite has no ShouldNotAttack / +0x111 check (Bite decompile), so that part of the sibling text was the game's rule.

## Fix
Shared method: the host replays a guest's attack through the game's own functions with the guest's puppet as the attacker (cell.guest_hit_game: DamageCell, Bite for the lethal one, a GetDamageAmount answer for that pair); the guest forwards the hp its electric chain wrote (cell.electric); the host runs the game's poison step for each puppet (cell.puppet_poison). Children: BUG-0714, 0715, 0716 fixed, BUG-0717 not-reproduced (the proxy shows the hit within 0.07-0.28 s and plays the host's reaction; a second TakeDamage would apply it twice). A mirror's other attackers are the host's pair or another partner's own hits: dropping them is right (counted, foreign_hits). Left as a known issue: the damage of a guest's poison clouds is not seen in the lab (docs/known-issues.md#issue-cell-guest-poison-damage; the grafted test part hurts nothing even for the host's avatar).

## Verification
no-fail-run: an umbrella; each child names its own FAIL and PASS runs.
Detector over the K2b final run on the merged branch out/lab/20261108-054842 (detect_* of all five stages, play_* of all five stages, every K2b test, SPORE_DETECTOR=20; report out of tools/detector_report.py): no signature of this class; the rows seen map to other tickets (BUG-0475, 0476, 0477, 0478, 0479, 0480, 0487, 0497, 0539, 0579, 0617, 0677, the BUG-0421 egg / guest-local rows, and Civ planet village / industry rows of K8's BUG-0659).

## History
- 2026-11-01: opened.
- 2026-11-08 (fix/w46-k2b-content, K2b of 0.7.11): every sibling item a closed child or covered; umbrella closed.
- 2026-11-08: released in 0.7.11.
