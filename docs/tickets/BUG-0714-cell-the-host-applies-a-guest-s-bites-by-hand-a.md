---
id: BUG-0714
title: Cell: the host applies a guest's bites by hand: no hurt animation, a knocked-out guest's lethal bite still kills, the lethal rule is not the game's
status: fixed
kind: bug
stage: Cell
reported: 2026-11-08
source: BUG-0428 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2b-content-a
commits: e81a56d9
tests: cell_guest_hit
signature:
docs: docs/re/cell.md#cell-guest-hit-game
related: BUG-0428, BUG-0078, BUG-0240, BUG-0509, BUG-0632, BUG-0350
---

## Report
From the BUG-0428 audit (K2b, 2026-11-08): the host applied a guest's bite (MSG_CELL_HIT) with a hand-made copy of DamageCell's gate and `hp -= dmg`, and a guest's lethal bite (MSG_CELL_EAT ACT_BREAK) with `hp = 0` and BreakOrSwallow, without the knockout gate of BUG-0509. The audit's claim "an invulnerable or spared creature dies" is the game's own rule (below), not a co-op bug.

## Root cause
Observed (runtime, lane 6, lab 20261108-034206 with the fix off): after a guest's bite the host's creature lost hp but played no hurt animation in the host's game (+0x1B0 never 0x10, while the host's own bite plays it); a lethal bite sent by a knocked-out guest (`coop.pool sendact`, an old guest's or one in flight) killed the creature. Static (Ghidra, GOG 2024): DamageCell 0xE7A7C0 after the hp plays PlayAnimation (0x10 for a bite) and schedules event 2 (the after-hurt reaction), runs 0xE57910 / 0xE576F0; Bite 0xE7B0A0 sets +0x178 on both cells and breaks the victim (BreakOrSwallow) whenever the damage reaches its hp or IsDamageable fails, with no ShouldNotAttack and no +0x111 check: an invulnerable cell dies of a lethal bite in the game too (BreakOrSwallow checks only +0x17C and the states 7-9). None of that ran for a guest's bite.

## Fix
`cell.guest_hit_game` (cellsync.cpp `guest_attack`, the one host path for MSG_CELL_HIT and ACT_BREAK): the host replays the guest's attack through the game's own function with the guest's puppet as the attacker: a MSG_CELL_HIT (what the guest's DamageCell decided) through DamageCell, the lethal bite (ACT_BREAK) through Bite (a first version sent type-4 hits through Bite: it broke rocks and meat a guest hit, cell_guest_bite lab 20261108-042210); GetDamageAmount (hooked) answers the guest's damage for that pair, KillCell inside counts it as the player's kill. A mouth's chunk (type 6) stays EatChunk's own rule. A knocked-out guest's lethal bite or kill is refused like its MSG_CELL_HIT (`guest_knocked_out`). `coop.pool` (host) prints `guest_attacks= guest_attacks_spared= guest_attack_kills= guest_attack_types=`; `coop.pool sendact c<key> break|kill` (tests, guest). Off: the hand-made path.

## Verification
Test `cell_guest_hit` (tools/tests/coop_cell_guest_hit.py: control host bite, three guest bites with the hurt animation read in the host's game, a knocked-out guest's lethal bite refused, a later one kills).
* FAIL with `SPORE_FIX_OFF=cell.guest_hit_game`: out/lab/20261108-043313 (the final code; no hurt animation after any of the 3 bites, the knocked-out guest's bite killed it), also out/lab/20261108-034206 (the first version)
* PASS: out/lab/20261108-044040, out/lab/20261108-050744 (the final code: hurt animation after each bite, the knocked-out bite refused, the later one killed); out/lab/20261108-034328 (the first version, type-4 hits through Bite)
* Related, the final code: cell_guest_bite out/lab/20261108-042814 (the BUG-0078 / BUG-0240 gate: spared species, +0x111, rock and meat take nothing) PASS; the first version failed it at the rock and meat (out/lab/20261108-042210): Bite breaks a cell IsDamageable refuses, so hits go through DamageCell now.

## History
- 2026-11-08: opened.
- 2026-11-08: fix/w46-k2b-content-a: reproduced, fixed (cell.guest_hit_game), test cell_guest_hit; title corrected (the lethal bite of an invulnerable cell is the game's rule).
