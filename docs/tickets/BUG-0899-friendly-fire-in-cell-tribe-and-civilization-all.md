---
id: BUG-0899
title: Friendly fire in Cell, Tribe and Civilization (all.friendly_fire covers the Creature stage only)
status: open
kind: feature
stage: all
reported: 2026-11-10
source: plan-0.7.12 F3 (BUG-0768 follow-up)
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
The approved F3 table of docs/plan-0.7.12.md has "Friendly fire: players can hurt each other (Cell, Creature, Tribe,
Civ)", default off. BUG-0768 made it a session setting (`all.friendly_fire`) and wired the Creature stage only.

## Root cause
How players are kept from hurting each other today (read in the code, 2026-11-10, feat/w46-settings-all):
* Cell: cellsync.cpp h_Pair skips every pair with a puppet (`g_pairs_skipped`): no bite, eat or swallow between a
  player's cell and a partner's puppet; poison and electric products on puppets are counted, never applied
  (own_product / note_foreign_hit). Each machine decides only its own cell's damage, so a hit on a partner needs a new
  message to the owner (like the Creature MSG_DAMAGE path) and the pair hook must let avatar-puppet pairs through.
* Tribe: every player's members belong to the host's one player tribe (`player_tribe()`), so the game's own AI and
  orders never attack them; h_CitizenTD only sees blows the game decides. Needs a hostile identity per owner (a tribe
  or relation per player) or a forced attack order plus a damage path.
* Civ: all players' vehicles and cities share the player's political id: the game's weapons never target them (as in
  Space, where the rivals mode gives a war partner's puppet kEnemyPol 0xFFFFFFFF: space_rivalry.cpp puppets_tick, the
  model to follow).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened (BUG-0768 follow-up); for the 0.7.13 plan.
- 2026-11-11: the owner decided: friendly fire in Rivals must work in every stage (required); done with Rivals 0.8, docs/11-rivals.md#rivals-friendly-fire-all-stages.
