---
id: BUG-9288
title: Cell: the host's evolution editor pulls the partner along (the partner gets the host's new body in place, its cell put at the level's start next to the host)
status: open
kind: bug
stage: Cell
reported: 2026-11-20
source: owner 2026-11-20 (friends build test)
role: host, guest
versions: 0.7.15.94
fixed_in:
branch: feat/start-choice
commits:
tests: cell_own_editor
signature:
docs: docs/tickets/BUG-9288-cell-the-host-s-evolution-editor-pulls-the-partn.md
related: BUG-9284, BUG-9265, BUG-9273, BUG-0508, BUG-1258, BUG-1263
---

## Report
The owner's test of the friends build 0.7.15.94 (host, a new Cell game, the friend on the official 0.7.15.5): "on
mating and changing parts (the Cell editor) the partner is teleported / pulled along with me". In separate lives each
player edits on their own; the other must not be pulled into the editor or moved.

## Root cause
✔ (the owner's log) Every return of the host from its Cell editor ran the share path of a stage change
(session_glue.cpp on_mode_change): "<host> evolved the species: everybody gets the new body" on every game, and for
the friend "evolved in place: ... keeps its world ... gets your new body in place". The friend's 0.7.15.5 then took
the host's new cell: a rebuild of its own cell, which the game puts at the level's start (the host's own cell is at
(100, 100) after every editor, the log's trace), so the friend appeared next to the host; once it was knocked out at
once and respawned next to the host. A guest that is not in the host's world at that moment (its own editor, a load)
is sent the whole world instead: a reload, its cell at the start as well.
✔ (lane A, both on 0.7.15.94, the guest on its own start-choice cell) the guest was not moved and kept its cell, but it
read "Player evolved the species: everybody gets the new body".

## Fix
`cell.own_editor` (session_glue.cpp): with own progress (separate lives) the host's return from its Cell editor is the
host's alone: no world share, no in-place body for the guests, the notice "<host> evolved its own cell (every player
evolves their own)". The friend's old version is refused at the join now (BUG-9284). Off: the share and notice of
0.7.15.

## Verification
Built (WSL). tools/tests/coop_cell_own_editor.py (cell_own_editor, twin cell_own_editor_off); the in-game run on lane
A: see History.

## History
- 2026-11-20: opened.
- 2026-11-20: fix on feat/start-choice.
- 2026-11-20: lane A, 0.7.15.95: the host's mating call and editor (a part moved, a name): the guest stayed in Cell, moved 3.7 m, kept its own cell, read "Player evolved its own cell": PASS. Twin (cell.own_editor off): the guest reads "everybody gets the new body": FAIL as expected (a 0.7.15.95 guest on its own cell is not rebuilt even then; the move came from the 0.7.15.5 guest's in-place body, refused at the join now, BUG-9284).
