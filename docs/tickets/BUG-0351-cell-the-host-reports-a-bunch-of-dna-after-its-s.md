---
id: BUG-0351
title: Cell: the host reports 'a bunch of DNA' after its second cell editor (cause not known)
status: wontfix
kind: bug
stage: Cell
reported: 2026-10-31
source: player reports 2026-10-31 (second batch: 11 zips, host and guest)
role: host
versions: 0.7.3.1, 0.7.3.3
fixed_in:
branch: fix/w22-reports2
commits: 6f18d53
tests:
signature:
docs: docs/re/cell.md#cell-editor-dna
related: BUG-0128, BUG-0345
---

## Report
Host mark (19:16:00 host time, Cell): "ive got bunch of DNA (probably a bug)". It came 52 s after
the host's second cell editor of the session (19:14:29-19:15:08, started by the host with 1/2 ready,
2 min 20 s after the first one), while the guest was in its own cell editor and then reloading.

## Root cause
Not known. Observed:
* Host food (S+0x1C) 312 at the first editor, 381 at the second, 405 at the mark (log `cell:` lines);
  the guest was a ghost until 19:12:46 (BUG-0342) and contributed no food before the mark.
* The cell editor's DNA budget is `FUN_00E51410(S+0x1C) - S+0x2C` (Ghidra GOG 2024, 0xE59800
  passes it to the EditorRequest; 0xE51410 interpolates the level table 0x01483B54, 7 ints per
  entry, by food: food 350 -> 25, 400 -> 28, so about 27 at food 381). The mod never writes S+0x2C
  (only S+0x1C/+0x20 through AddFoodPoints).
* Nothing in the logs shows a DNA value in Cell (`dna=-1`; the shared DNA, MSG_DNA, is a Creature+
  thing).
Inferred candidates: (a) the shared food: every player's meals raise every player's food, so the
DNA of both players arrives (by design, never explained to players; after 19:16:08 the guest's
meals were applied on the host, food 405 -> 1000 in 2.5 min); (b) something reset or did not keep
S+0x2C (spent DNA) across the host's evolution / RebuildLevel; (c) the player meant something else.

2026-11-01 (fix/w30-progress, the same players' 0.7.3.3 reports, with the diagnostics of the
fix below): **by design plus the game's own editor, not a co-op bug.** Host mark (Creature, 13:21):
"i keep getting alot of DNA (64) randomly ... since cell stage". Observed:
* Cell, host log: `DNA on entering Cell` after the first cell editor: food=267 spent=**-40**
  dna_for_food=20 budget=60 (at the request: spent=0, budget=20); after the second editor spent=-19
  (21 spent), after the third -20. The mod never writes S+0x2C: the game's cell editor wrote a
  negative "spent", i.e. the edited cell is worth 40 DNA less than the starting carnivore cell
  E3B1B67B (the model 0.82 -> 0.55 in size, diet carn -> omni). Inferred: the editor's sell-back of
  the starting cell's parts (vanilla; not checked in a solo game). The guest's game got the same
  values with the host's world share.
* Cell: food is shared by design (the guest's meals `eat: from 1 ... applied` on the host): food
  375 -> 849 in 2.5 min, so DNAForFood 26 -> 55 between two editors.
* Creature: the pack's DNA is shared by design (docs/03-gameplay.md: a kill or befriend by any player
  gives DNA to everyone): the host's total jumped 38 -> 65 in 10 s from its own kills while the guest
  was in its editor (4 dead NPCs nearby), and later by the guest's kills (guest log `dna: host total
  70 -> +9`, `100 -> +30`, ...). The mark's "64" is the host's 65 at that moment. Only DNA a guest
  earns itself and sends (MSG_DNA_ADD) shows "<name> earned N DNA" on the host; NPC kills a guest
  makes on the host's NPCs are paid by the host's own game without a line, so they look random.
Status wontfix (by design); a clearer notice for shared DNA would be a feature.

## Fix
Diagnostics only (fix/w22-reports2, no behaviour change): `cellsync::dna_note` logs
`cell: DNA <when>: food= plant= spent= dna_for_food= budget=` at each cell editor request (h_Submit,
also after the vote) and on entering Cell (a load, an editor's return). `dna_for_food` is the game's
`DNAForFood` 0xE51410 (fastcall, ECX = food), `budget` = dna_for_food - spent, what the editor gets.
The next report with such a mark shows the numbers. Seen in lab 20261031-132041 (both lines, also at the
request of the cell_editor_invuln editor); the fixture plays at food 0, so DNAForFood was only seen
returning 0 (the call works, no fault). Status stays open until then.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-31: opened from the second batch of the 2026-10-31 report zips (host mark).
- 2026-10-31: the diagnostics added on fix/w22-reports2.
- 2026-11-01: fix/w30-progress: the 0.7.3.3 reports explain it (game editor refund, shared food and DNA); wontfix (by design).
- 2026-11-01: the shared DNA now names its cause on screen and in the logs (`dna: a -> b cause=...`, "+9 DNA from Bob's hunt", Cell "+10 food (DNA) from Bob's meals"): BUG-0389 (feature), fix/w31-creature-gate.
