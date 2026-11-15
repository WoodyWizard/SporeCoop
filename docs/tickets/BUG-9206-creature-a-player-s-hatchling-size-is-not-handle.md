---
id: BUG-9206
title: Creature: a player's hatchling size is not handled: tiny until it grows on its own game, adult x1.15 on partners' screens
status: open
kind: bug
stage: Creature
reported: 2026-11-14
source: owner
role: 
versions: 0.7.8-lineages54-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0014 BUG-9181 BUG-9194
---

## Report
Owner play test 2026-11-14 (screenshots): after evolving, a guest looked larger than the host on the host's screen; and a player coming out of the egg is so small it is practically invisible until it grows.

## Root cause
Inferred from logs and code (medium-high confidence), one root: a player's hatchling state (size 0.1, age 0) is not handled for players. (a) Own game: a respawn or hatch sets the avatar's scale to 0.1 and a family member's greeting normally sets the real size (docs/re/creature.md 13); a guest's family are synced copies with no AI, so nobody greets it and it stays at 0.1 until `hatch_tick` (npc.cpp around 1878) steps in, which waited 23 s after the editor return (guest log 18:56:54 "the hatchling waited for its family ... it grows up now"). (b) Partners' screens: a puppet is spawned adult and made 15 % bigger (modes.cpp around 152, `set_scale(p, game::scale(p) * 1.15f)`); a player's own size is never sent outside Scenario (`captain_scale`, modes.cpp around 85 and 481), and `puppet_back` (evolve.cpp around 242) gives the new body the old body's size. Fix idea: send the own size in Creature too (the captain_scale field), apply it to the puppet, and set the normal hatchling size at once when hatch_tick sees a waiting hatchling.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-14: opened.
