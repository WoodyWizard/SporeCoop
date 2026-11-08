---
id: BUG-0315
title: Cell -> Creature: the stage switch itself has no vote (split from BUG-0178 item 1)
status: fixed
kind: feature
stage: Cell
reported: 2026-10-30
source: BUG-0178
role: host
versions: 
fixed_in: unreleased
branch: fix/w46-e1-features
commits: 3365fc83
tests: switch_vote (coop_switch_vote.py)
signature:
docs: docs/re/cell.md#cell-switch-to-creature, docs/06-testing.md#testing-switch-vote-editor-now
related: BUG-0178, BUG-0516
switch: core.switch_vote
---

## Report
Split from BUG-0178 item 1 (docs/known-issues.md): the host's click on the History's check starts the
Creature stage at once and the guests follow; only the first creature editor waits for the vote
(docs/09 9.3 wants a vote for the switch itself). A feature, not a bug.

## Root cause
Observed (Ghidra, lab 20261108-035847): History's check reaches `SporeCoop::CellStage_ToCreature` 0xE62440 through the
cell event dispatcher 0xE7E130 case 0x1F (return address 0xE7E36E; the other caller is cCellModeStrategy::HandleMessage
0x47C1D19). It ends the cinematics, runs CreatureStart_FromCell and switches the mode to Creature at once; nothing in
the mod stood between the click and the switch, so the vote came only with the first Creature editor
(docs/re/cell.md#cell-switch-to-creature).

## Fix
`core.switch_vote` (coop.cpp h_CellToCreature): in a session the host's call waits for an action vote ("wants to move on
to the Creature stage", 60 s, "Start now"); when it passes, the game's own switch runs from outside the frame (the
vote's action at the busy gate). The first Creature editor's vote within 300 s after it counts every guest that plays
the host's Creature world with an avatar as ready (no second ready round). A guest's own call in the host's Cell world
does not switch it alone: during the vote it is its I'm ready, otherwise a notice. Off: as in 0.7.10. Kept behind the
switch so it can become a session setting (docs/plan-0.7.12.md F3).

## Verification
`switch_vote` (coop_switch_vote.py, lane 4): both games stay in Cell after the host's check, the guest's window shows
I'm ready, the guest's own check keeps it in Cell, its click moves both to Creature, both first editors open without
another click.
* PASS: out/lab/20261108-035847.
* FAIL with `SPORE_FIX_OFF=core.switch_vote`: out/lab/20261108-040129 (the host's game was in Creature 8 s after its
  check, so was the guest's).

## History
- 2026-10-30: opened.
- 2026-11-08 (fix/w46-e1-features, E1 of 0.7.11): implemented, core.switch_vote.
