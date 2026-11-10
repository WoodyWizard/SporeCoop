---
id: BUG-0868
title: Feature idea: personal Cell edits (mating call) without a Ready vote
status: open
kind: feature
stage: Cell
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages30-dev (coop.cpp personal_cell_edit)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
In Cell every mating call (a new part, a new name) asks all players to press Ready, and the partners are held in
the vote. The fork opens these personal Cell editors without a vote; partners only get a notice and keep playing.

## Root cause
Not a bug: our editor vote (since 0.7.4) holds every editor request in a stage (coop.cpp h_Submit path).

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages lineages30-dev, mods/coop/coop.cpp personal_cell_edit / submit_native_editor); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* In Cell, the EditorRequest's editor id (request+0xC) tells the kind: the Cell growth table's personal editors are
  0x1D2EC0A0 and 0x1D2EC0A4..0x1D2EC0A7 (Steam / GOG 2024, seen live); the Creature landfall editor 0xFD4902BD stays a
  shared transition with the vote.
* For a personal one: skip the vote (`!personal_cell_edit(req, mode)` in the vote condition) and send partners a
  passive feed notice. The BUG-0560 editor budget replay still applies.
* Needs the creation stream to update the player's cell live on the partners (we have it) and partners kept in the
  world after the host's editor return (we have BUG-0508 in-place evolution).
* Overlap: plan-0.7.12 F3 could make it a session setting ("Cell edits need a vote: on/off"); F5 (one creature for
  everyone) must keep the vote in its mode.

## How to test
Lab, 2 instances, Cell: the host opens the editor through the mating call. With the feature: the guest never sees a
vote and keeps moving (its position changes during the host's edit), the host's new cell appears on the guest after
save. The landfall (Creature) editor still asks for the vote.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
