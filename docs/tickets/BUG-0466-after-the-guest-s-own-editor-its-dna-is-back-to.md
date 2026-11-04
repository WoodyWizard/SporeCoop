---
id: BUG-0466
title: after the guest's own editor its DNA is back to what it had before (DNA spent in the editor is not kept)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: guest
versions: 
fixed_in: 0.7.4
branch: fix/w40-editor
commits: 43ef1445 9681b9d3
tests: editor_sync_guest (tools/tests/coop_editor_sync.py --case guest), dna_spend (--case spend)
signature:
docs: docs/re/creations.md#creations-own-editor-dna
related: BUG-0423, BUG-0368, BUG-0278
switch: creations.own_editor_dna, core.dna_spend
---

## Report
Guest's mark (s 1062, 18:21): 'after editor i still have the same amount of DNA as when I went in'. The audit found (G6, BUG-0423): shared.cpp sets have=false on a mode mismatch and drops the local delta, so a spend right after an editor is undone (free DNA). Class G6.
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lab 20261103-141812 and 20261103-143936, lane 7, creature fixture, real input): in the guest's
own editor of a vote (open_own_editor: an EditorRequest without the evolution tag +0x90) the guest added
a mouth, the editor's DNA went 40 -> 15, "done" saved the new creation, and the game's own
GetEvolutionPoints stayed 40: the game never charges this editor. The creature game's message handler
0xD42E50 applies an editor's result (message 0x30C11C7, data +0xC = the request's tag) only for the
evolution tag 0x64AA168 (and 0x12191CA from Cell); only then does the stage's return set the DNA to the
result's remaining DNA (data +0x34, 0xD445AA). No mod code reset the value (no "dna:" line after the
editor in the guest's log). So the parts were free, on the guest's screen and in the party's total.
The audit's hypothesis (shared.cpp have=false on a mode mismatch) is a different module (Tribe food,
Civ / Space money): not the cause here. Its Creature counterpart was real too (observed, lab
20261103-143105): a guest's own spend (AddEvolutionPoints < 0 in its game) was never sent (the guest
forwards gains only) and the host's next total (every 2 s) gave it back.

## Fix
`creations.own_editor_dna` (coop.cpp, hook of 0xD42E50 `SporeCoop::CreatureGame_HandleMessage`): the result of
the guest's own editor (0x30C11C7, no evolution tag, not cancelled +0x44 == 0) charges the price
through the game: DNA = the result's remaining DNA (+0x34), notice "your new parts cost N DNA".
`core.dna_spend` (the class, G6): a guest samples its own DNA while it plays the host's world (the
Creature stage epoch: an editor keeps it, a load starts over); a drop of its own (a spend, the own
editor's price counted at once) is held while the host is away and sent once as a negative
MSG_DNA_ADD; the host takes it off the party's total (never below 0; owed while it has no creature);
a host total that arrives first keeps the unsent spend off. Older hosts ignore d <= 0 (no change).

## Verification
FAIL: SPORE_FIX_OFF=creations.own_editor_dna editor_sync_guest, lab 20261103-143936 (lane 7: the editor showed
40 -> 15, back in the stage the guest's DNA 40, the host's 40).
PASS: editor_sync_guest, lab 20261103-143836 (lane 6: guest 40 -> 15 at once, host 40 -> 15, both 15 for 20 s).
Class: FAIL SPORE_FIX_OFF=core.dna_spend dna_spend, lab 20261103-143105 (guest's -25 given back: 40/40);
PASS dna_spend, lab 20261103-142938 (host and guest 15).

## History
- 2026-11-02: opened.
- 2026-11-03: fix/w40-editor: the own editor charges its price (creations.own_editor_dna) and a guest's spend reaches the host (core.dna_spend); fixed (unreleased).
- 2026-11-03: released in 0.7.4.
