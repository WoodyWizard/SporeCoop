---
id: BUG-0510
title: Cell and Creature: players get much more DNA than in the single-player game (3-4x); a guest's meals arrive as one lump in the host's open Cell editor (+68, '100 DNA')
status: fixed
kind: bug
stage: Cell
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: both
versions: 0.7.3.4, 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-dna-social
commits: f384cc0f 9d90117e 0b0f811c
tests: dna_pace (tools/tests/coop_dna_pace.py), dna_pace_cell (--cell)
signature:
docs: docs/re/creature.md#creature-dna-pace
switch: core.dna_pace
related: BUG-0351, BUG-0345, BUG-0466, BUG-0423
---

## Report
Three reports:
1. 0.7.3.4 forum post, follow-up: far too much DNA in the game, like 3-4x the default.
2. Host's mark (0.7.3.6, report 20261103-185644): 'I got 100 DNA in cell stage editor'; the screenshot shows 85 DNA in the Cell editor. Host log: at the editor request food=256 budget=19 (dna_for_food=19); 7 s after the editor opened '+68 food (DNA) from the guest's meals' (one +50 plant meal and three +6 meat meals sent by the guest while 'no host cell with it').
3. Direct message: the guest spawns in the Cell stage with its PREVIOUS cell creature (from another world) and that seems to give the host 100 DNA because of all the parts the guest has.
Guest log (report 20261104-005925): on joining Cell `dna: food 24 -> 170 cause=meal:255+146` (the host's total), its own cell is a creation of another world (BUG-0345); later in Creature `dna: 20 -> 78 cause=unknown` right after a world load.
Target: 0.7.4 (Cell / Creature).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: the party's food / DNA is one shared total, every player's meals add to it (by design), so two players earn about twice as fast; the guest's meals that reach the host while the host is in its editor are credited at once into the editor's DNA (+68 at once). The 'parts give DNA' idea is not supported by the logs: the +68 are meals, one of them a +50 plant meal.
Inferred, not checked: (a) the game's Cell editor budget is meant to be the host's own progress; the shared meals make it 2-4x; (b) the +50 meal is a big plant chunk the guest's own (other-world, BUG-0345) cell can eat; (c) `cause=unknown` jumps after loads are the host's total applied to a guest. Decide the design (shared total scaled by the number of players, or only the eater's meals count toward the editor), then fix. Size: M.
<!-- observed (runtime, logs, disassembly) vs inferred -->

Observed in the lab (lab 20261103-180306 / 20261103-180539, the fix off): two players each earning 20
(Creature AddEvolutionPoints, Cell AddFoodPoints) made the shared total grow by 40 on both games.

## Fix
Design (orchestrator): the one shared total stays, every gain entering it is scaled by 1/N, N = the
players playing the host's world now (the host + guests with its mode and world, min 1); spends are
whole. Each gain is scaled once where its game made it (host: own AddEvolutionPoints in on_dna, a
guest's social win; guest: its forwarded gain before MSG_DNA_ADD; Cell: the eater's own meal in
h_AddFood, integer points with the remainder kept). Not scaled: goal card rewards (once per party),
solo DNA earned while the host was away, the guest's own world. The host decides with coop.ini
[coop] dna_pace (1 default, 0 = the full sum) and announces the divisor in an optional last byte of
MSG_DNA and MSG_CELL_FOOD. The open Cell editor: its budget is fixed at the request (0xE59800), meals
arriving during the mating cinematic count at the next editor (now 1/N); nothing more held
(docs/re/creature.md#creature-dna-pace). Switch `coop.fix core.dna_pace off`.

## Verification
FAIL: SPORE_FIX_OFF=core.dna_pace dna_pace, lab 20261103-180306 (host and guest +40 for two gains of 20);
dna_pace_cell, lab 20261103-180539 (+40 food on both).
PASS: dna_pace, lab 20261103-180136 (+20 on both, a guest spend of 10 whole, dna_pace=0 gives +40);
dna_pace_cell, lab 20261103-180425 (+20 food on both, dna_pace=0 +40).

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage), three reports merged.
- 2026-11-03: fix/w40-dna-social: each gain counts 1/N players (core.dna_pace, coop.ini dna_pace); fixed (unreleased).
- 2026-11-03: released in 0.7.4.
