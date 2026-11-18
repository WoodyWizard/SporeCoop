---
id: BUG-1239
title: Cell: while one player is in the evolution editor the other player's screen stays black (seen on a players' video, before 0.7.15)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-18
source: owner: players' video
role: host
versions: <0.7.15
fixed_in: unreleased
branch: fix/w52-evoeditor
commits: ac41e25c
tests: evo_screens_cell_both, evo_screens_cell, evo_screens_cell_noready, evo_screens_cell_one, evo_screens_switch_slow
signature: the host's game in Cell (or Creature at its start) with a black picture behind the co-op window's vote ("wants to evolve ... ready 1/2") while the guest's game is in Editor
docs: docs/re/cell.md#cell-vote-black-screen
related: BUG-1240, BUG-0315, BUG-0801
---

## Report
A players' video (players on a version before 0.7.15), seen by the owner: in the Cell stage, while one player was in
the evolution editor (the mating call / evolve), the other player's screen stayed black. Expected (docs/09-coop-design.md
9.3, 9.4.1): the vote, then each player in its own editor; for a guest's own mating call the other plays on.

## Root cause
Observed (lab, `tools/tests/coop_evo_screens.py`, both games' screens shot every 2 s; the picture is measured beside the
co-op window, which covers the middle of the view):
* The host's mating call in Cell: the game fades its picture out after the mating scene and asks for the editor; the
  mod holds that request for the vote. The host's screen is black behind the vote's window until every guest pressed
  I'm ready (`01_host_vote_host.jpg` of out/lab/20261118-094043 evo_screens_cell_noready: 60 s black when the guest
  never presses). The guest meanwhile plays on: its screen stayed drawn in every run (vote with and without its ready,
  the host's editor, the host's return; out/lab/20261118-094043).
* A guest's own mating call opens its own cell editor without a vote (9.4.1). A guest in that editor cannot press
  I'm ready, and the vote waited for it all the same: when the host called its mate while the guest edited its cell,
  the host stared at the black screen for the vote's whole 60 s (`host editor 58 s after its mate (guest Editor)`,
  out/lab/20261118-101725, frame 01_host_vote_guest_editing_host.jpg: black game, the lobby "Bob Editor", "ready 1/2").
  That is the report's "black while the other is in the editor" (inferred: who of the players was host is not known
  from the video).
* Also seen, not changed: at the start of the Creature stage the host's game is black behind the arrival vote of the
  first Creature editor (BUG-0315) until the guest arrived (2-10 s on a clean link, up to the vote's 60 s over a slow
  one, out/lab/20261118-100546 host shots); and the guest's own picture is black for 4-14 s after its load of the
  host's Creature world (the game's stage-entry pause, BUG-0313), longer at low fps. A guest that is still on its way
  when that vote ends lost its own editor: BUG-1240.
* Not reproduced as a black screen: the guest's screen during the host's Cell vote and editor (with I'm ready, without
  it, with creature.mode host: out/lab/20261118-094043 evo_screens_cell / _noready, out/lab/20261118-101230
  evo_screens_cell_one), the host's screen while the guest is in its own cell editor (same runs).


## Fix
`core.vote_guest_editor` (vote.cpp vote_tick): a guest whose game is in an editor (its own evolution) is not waited for
by the host's vote, like a guest in another world (BUG-0801); the vote ends as soon as the others are ready, so the
host's editor opens at once instead of after 60 s of a black screen. `coop.vote` counts such votes. Off: waited for
(0.7.15). The arrival vote's wait at the Creature start is left as it is (an older guest has no BUG-1240 fix and would
lose its own editor if the host stopped waiting).


## Verification
Test `evo_screens_cell_both` (coop_evo_screens.py --part cell-both, cell fixture, real clicks: the guest's mating
call and swim to its mate first, then the host's; both edit and save their cells): the host's editor opens without
waiting for the guest in its editor and the host's picture is not black for 20 s.
* FAIL: out/lab/20261118-101725 (`SPORE_FIX_OFF=core.vote_guest_editor`, lane 4): the host's editor 58 s after its mate,
  the host's screen black behind the vote window meanwhile.
* PASS: out/lab/20261118-101722 (lane 3), out/lab/20261118-101728 (lane 5): the host's editor 0 s after its mate.
* The other Cell paths (no black on either screen): out/lab/20261118-094043 evo_screens_cell, evo_screens_cell_noready;
  out/lab/20261118-101230 evo_screens_cell_one.


## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w52-evoeditor): the black screens measured on every evolution path; fixed the host's vote waiting for a guest in its own editor (core.vote_guest_editor); the slow Cell -> Creature path is BUG-1240.
