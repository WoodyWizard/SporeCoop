---
id: BUG-0796
title: Creature: a partner's animation is played on a non-creature puppet (a Cell cell or a Tribe marker) around a stage switch
status: open
kind: crash
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0091 BUG-0426
---

## Report
No player report. A partner's reliable MSG_ANIM that arrives while this game shows that partner as a cell (Cell) or a pointer marker (Tribe) can crash this game.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): coop.cpp:3938-3955 on_remote_anim skips the creature_listed check only for creature adapters; any other puppet goes straight to game::play_animation (cCreatureBase::PlayAnimation), which reads +0xB54 (the animated creature) of a cCellObjectData in the pool or of a heap Marker. Inferred: reachable around Cell -> Creature (the host's avatar plays its first animations while the guest's buffer still samples the host's Cell state) and Creature -> Tribe (a resent reliable anim arriving after the marker puppet exists). Confidence: high on the code, low-medium on reachability (window 0.1-1 s).

## Fix
Not fixed yet.

## Fix idea
Return in on_remote_anim unless creature_puppet(it->second.puppet_adapter) (and the puppet is listed).

## How to test
Test hook: `coop.anim inject <player> <id>` on the guest while it shows the host as a cell (both in Cell). Main: PlayAnimation on the cell (fault or a logged probe); with the fix: dropped and counted.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
