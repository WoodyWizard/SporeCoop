---
id: BUG-1470
title: Own stage history per player: consequence cards, trait progress and Space archetype are each player's own (ideas from PR #207 and #217 by IltonBJSilva)
status: open
kind: feature
stage: Cell, Creature, Tribe, Civ, Space
reported: 2026-11-23
source: plan, PR #207, PR #217
role: 
versions: 0.7.17
fixed_in:
branch: feat/w70-history
commits: aecef640
tests: own_cards, own_cards_store, own_cards_space, own_cards_shared (tools/tests/coop_own_cards.py); validate_tests test_history
signature:
docs: docs/re/creations.md#creations-own-history, docs/04-netcode.md#netcode-player-history, docs/01-architecture.md#arch-player-store
related: BUG-1471, BUG-1469, BUG-1475
switch: core.own_history
---

## Report
Each player's consequence cards (Cell diet, Creature, Tribe, Civ, Space archetype) and the trait progress behind them are its own; one player's choices never change another's. Design (hook cPlayer::AddTraitProgress 0xC75840 inside an Actor scope, mods/coop/actor_scope.h of WP-0), wire (session 1.3, MSG 75 PLAYER_HISTORY), store record `history`, switches and tests: docs/plan-0.7.17.md item 3. Credit IltonBJSilva (PR #207, #217).

## Root cause
Design (no bug: a feature). Every game has one cPlayer loaded from the host's save: the five consequence cards
(+0x10FC..+0x110C) and the Creature trait progress (+0x1278 social, +0x127C combat); the wsync row "stages" wrote the
host's cards into every guest's game every 0.5 s, and only the host's game runs a stage end. So every guest's History,
its editors' consequence abilities (vote.cpp copies the cards from cPlayer) and in Space its archetype were the host's.
RE (from PR #207 / #217 by IltonBJSilva, every address checked again by objdump of GOG 2024):
docs/re/creations.md#creations-own-history; symbols in overrides_dc04aee5.sym.

## Fix
mods/coop/history_logic.h (portable rules), wire/history.h (MSG_PLAYER_HISTORY 75, session 1.3), history.cpp
(glue). In creature.mode own the host keeps a record per guest (per-player store record `history` v1, under its stable
key; first sight: a copy of the host's cards and progress). When the host's world has a stage done that a record has
not: Cell = the guest's own game's Cell_GetConsequenceTrait (K_CELL report), Creature = Creature_GetConsequenceTrait
run with the guest's progress lent into the host's cPlayer (swap ledger), Tribe / Civ = the host's (provisional owner
choice, plan question 3: one shared tribe and nation), Space = SpaceArchetype_FromTraits of its four cards; a stage
the host has not done is cleared. The host sends each guest its record (K_RECORD, only to a peer with session 1.3:
old-peer guard); the guest's game writes it with cPlayer_SetConsequenceTrait while it plays the host's world, the
wsync row "stages" leaves it alone (history::guest_owns), in Space its empire takes its own archetype
(SpaceGame_SetArchetype(game, false, false), once per world and card, only when the empire's index differs).
creature.mode host: one shared history as before. The host's save keeps the host's cards only (the lent progress is put
back right after the game's call). Switches: `coop.fix core.own_history off` (the shared history of 0.7.16.1, nothing
sent), `coop.fix space.own_archetype off` (the guest's empire keeps the host's archetype). Credit: IltonBJSilva (PR
#207 own cards and their RE, PR #217 the own archetype tools, PR #218's note on games started after Cell).
Limits: the Creature rule adds the host world's History timeline Cell value; with the shared Cell food (own wallet)
a guest's Cell card mostly equals the host's (its own diet counts with WP-P1's own progress); whether switching the
archetype removes the host archetype's tools was not traced.

## Verification
Unit: tests/validate_tests.cpp test_history (seed, credit, the decide rules, Tribe / Civ follow the host, a reload
clears, an empty start, the archetype gate, the peer gate, store and wire codecs), test_manifest (MSG 75), fuzz_decoders;
`make test` passes. Lab: pending night run: own_cards, own_cards_store, own_cards_space, own_cards_shared (PASS wanted)
and the twins own_cards_off, own_cards_store_off, own_cards_space_off (FAIL wanted). Not run yet (no lab by day).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 3 (WP-H1), branch feat/w70-history.
- 2026-11-23: implemented on feat/w70-history (aecef640): history.cpp, history_logic.h, MSG 75; lab tests written, waiting for the night run.
