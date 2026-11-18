---
id: BUG-1249
title: Cell: after the editor the guest sees its old DNA until the host leaves its editor
status: fixed
kind: bug
stage: Cell
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in: unreleased
branch: fix/w53-cellvote
commits: 707817ca 6878398c
tests: evo_screens_cell_dna
signature: guest log "cell: DNA on entering Cell: ... spent=N" after its own editor, then the HUD's DNA back at the host's count within 0.5 s
docs: docs/re/cell.md#cell-mating-round
related: BUG-0560, BUG-0466
switch: cell.guest_spend
---

## Report
A players' video: the guest leaves the editor with bought parts but sees its old DNA (15) until the host leaves its own editor; then it drops to 9.

## Root cause
Observed (Ghidra GOG 2024 and lab 20261118-123203, `cell.guest_spend` off): the Cell DNA is DNAForFood(food) - spent
(S+0x2C). The return from the guest's own cell editor (`Cell_EditorResult` 0xE809A0) writes the guest's S+0x2C =
DNAForFood(food) - remaining (spent 0 -> 15 in the guest's log), and the host's wsync row cell_spent (BUG-0560: the
guest's spent count is the host's) put the host's count (0) back within half a second: the HUD showed 28 again after a
15 DNA part (frame 02_guest_back_dna_guest), the part was free and the host's DNA never paid for it. The count changed
on the guest only when the host's own count changed: its editor (the report's "until the host leaves its editor").
Inferred: the video's 15 -> 9 is the host's own purchase then, the guest's was lost.

## Fix
`cell.guest_spend` (cellsync.cpp, wsync.cpp): the guest measures its own Cell editor's spend (S+0x2C on its return minus
before) and sends it (MSG_CELL_FOOD of 0 food with the spend after the meal number; an older host ignores the field);
the host adds it to its count at once in Cell, or after its own editor's result when it is in an editor; the new wsync
row cell_spend_ack (id 18, in the same message as cell_spent) tells the guest which spend is in the count, and until
then the guest shows the host's count plus its own spend. The party's DNA pays the guest's parts (as BUG-0466 in
Creature). `coop.pool spend` prints the bookkeeping. Off: nothing sent (0.7.15). Family minors cell 1.1 / npc 1.1.

## Verification
Test `evo_screens_cell_dna` (coop_evo_screens.py --part cell-dna, cell fixture, food 400 = 28 DNA; real clicks: the
guest's mating call and swim, a part dragged from the editor's palette, done; then the host's mating call, the vote,
the host's editor): the guest's DNA after its editor, the host's, and both after the host's editor.
* FAIL: out/lab/20261118-123203 (`SPORE_FIX_OFF=cell.guest_spend`, lane 2): the guest 28 -> 28 (its log "spent 0 ->
  15"), the host 28.
* PASS: out/lab/20261118-123223 (lane 3): the guest 28 -> 13 and kept for 25 s, the host 13, both 13 after the host's
  editor; frame 02_guest_back_dna_guest shows 13 in the HUD.

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-cellvote): reproduced (the host's spent row undid the guest's editor), fixed with cell.guest_spend.
