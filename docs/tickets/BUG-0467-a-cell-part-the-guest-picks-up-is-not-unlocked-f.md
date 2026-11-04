---
id: BUG-0467
title: a Cell part the guest picks up is not unlocked for the host
status: fixed
kind: bug
stage: Cell
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 
fixed_in: 0.7.4
branch: fix/w40-editor
commits: 43ef1445 9681b9d3
tests: cell_parts (tools/tests/coop_cell_parts.py)
signature:
docs: docs/re/cell.md#cell-parts-shared
related: BUG-0401, BUG-0316
switch: parts.shared_cell
---

## Report
Host's mark (s 433, 17:31): 'My guest just took a new piece in cell stage but i dont get it unlocked.' parts.shared (BUG-0401) covers Creature only; Cell part tokens are per game (BUG-0316). Class G4.
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (objdump GOG 2024, lab 20261103-142941 with the fix off): the Cell stage's collected parts are 13
ints at cCellSerializableData +0x34 (0 locked, 1 unlocked, 2 new), set by 0xE5D7B0
(`Simulator::Cell::CollectPart`, the part token's collision) in the collecting player's game only;
the cell editor's palette is built from them (0xE4FAA0). parts.shared (BUG-0401) covers the creature
parts (cPlayer +0x10E8) only, so a guest's Cell part never reached the host (the host's state stayed 0,
its mating-call button too) and the other way round.

## Fix
`parts.shared_cell` (parts.cpp, MSG_PARTS kinds 3 and 4, older SporeCoop ignores them): every game polls
the 13 states in a live Cell game (1 Hz); a guest in the host's world sends the parts it has and the
host lacks, the host collects them through the game's own CollectPart (outside the frame: the part
pop-up, the mating-call button, the palette's items) and sends its set (at once on a change, else
every 10 s); a guest collects what it lacks the same way. Nothing is locked. Notice "Bob found a new
cell part: it is in your editor too". Also `coop.pool`'s `parts=` read the states as bytes (13 ints
now). Test helper `coop.parts cell [collect <i>]`.

## Verification
FAIL: SPORE_FIX_OFF=parts.shared_cell cell_parts, lab 20261103-142941 (lane 7: the guest's part 0 stays
0 on the host, the host's mating-call button 0, the host's part 1 stays 0 on the guest).
PASS: cell_parts, lab 20261103-142443 (lane 7: guest part 0 -> host state 2 and button 1 within 15 s;
host part 1 -> guest state 2).

## History
- 2026-11-02: opened.
- 2026-11-03: fix/w40-editor: the party shares its Cell parts (parts.shared_cell); fixed (unreleased).
- 2026-11-03: released in 0.7.4.
