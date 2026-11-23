---
id: BUG-1473
title: Difficulty for 3-4 players: a session setting scales the NPCs' challenge (all.party_difficulty; RE of the difficulty tunings first)
status: open
kind: feature
stage: Creature, Tribe, Civ, Space
reported: 2026-11-23
source: plan
role: host
versions: 0.7.17
fixed_in:
branch: feat/w72-difficulty
commits: 01d85cdd, b9fb8384, 386a67ce
tests: party_difficulty, party_difficulty_auto, party_difficulty_off
signature:
docs: docs/03-gameplay.md#gameplay-party-difficulty, docs/06-testing.md#testing-party-difficulty, docs/re/creature.md#creature-difficulty-tunings, docs/re/space.md#space-difficulty-keys
related: BUG-1288
switch: core.party_tuning
---

## Report
With 3-4 players the NPCs fall too fast. RE first: which DifficultyTuning keys each stage reads for NPC health, NPC damage, raids and epics; the host answers them multiplied by the setting's factor. Setting values, switch `core.party_tuning` and the 3-game test: docs/plan-0.7.17.md item 5.

## Root cause
Design (feature). RE first (static, Ghidra on GOG 2024 + the shipped prop files; docs/re/creature.md#creature-difficulty-tunings):
the game's difficulty is cPlayer +0x1248 (0 easy, 1 normal, 2 hard). cGameModeManager holds three PropertyLists per game
mode (+0x3C, loaded from the .rdata table 0x145D558 by 0xB1DBA0); Space reads 3-float keys of its own list through
DifficultyTuning_GetFloat 0xC75DD0 (11 keys, docs/re/space.md#space-difficulty-keys). Observed: no stage's lists hold an NPC
health or NPC damage key (Creature: unknown-role ints / arrays; Tribe: AI attitude arrays and timers; Cell, Civ, Space: the
same list at every level; Space's keys: the player's ship health, spice prices and production, colony events), and most
readers copy their keys once at a mode's start. Answering the keys multiplied would not touch NPC health or damage and
would change the economy and the AI. So the factor goes on the fights in our own hit paths on the host.

## Fix
Session setting `all.party_difficulty` (All stages): 100 % / 125 % / 150 % / 200 % / auto (+25 % per player over two).
Default 100 % (`party::kDefaultChoice` in party_difficulty_logic.h; owner question 5 open, auto is a one-line change).
On the host: a player's hit on an NPC x 100 / P, an NPC's hit on a player x P / 100 (party_difficulty_logic.h, portable):
npc.cpp damage_filter (the host's creature both ways, the NPC hits forwarded to guests as MSG_DAMAGE) and MSG_DAMAGE_REQ
(a guest's hit), tribe.cpp h_CitizenTD (the player tribe against other tribes, animals, epics), civ_shots.cpp
h_VTakeDamage (vehicles by owner) and civ.cpp b_damage (a player's vehicle on a building), squadron.cpp h_TakeDamage
(the host's ship and the guests' puppets against NPC ships) and apply_hit (MSG_UFO_HIT). Not scaled: Cell, an NPC's hit
on the players' Civ buildings, a place a guest leads in Space (BUG-1288). No wire change (the setting goes by name).
`coop.party` shows the percentage and the scaled hits; `coop.hp npchit 0 <dmg>` lets an animal hit the host's creature.
Switch: `coop.fix core.party_tuning off` (0.7.16.1: the game's hits at any setting).

## Verification
Open, waiting for the lab run. pending night run: party_difficulty, party_difficulty_auto (must PASS),
party_difficulty_off (SPORE_FIX_OFF=core.party_tuning, must FAIL); each takes two lanes (A: host + Bob, B: Carol).
Unit tests: tests/party_tests.cpp (percent, auto by players, scale by sides, hits to kill, the setting row) and
validate_tests test_settings pass in make test.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 5 (WP-D1), branch feat/w72-difficulty.
- 2026-11-23: RE (01d85cdd), the setting and the scaling (b9fb8384), lab test and twin written on feat/w72-difficulty; waits for the night run.
