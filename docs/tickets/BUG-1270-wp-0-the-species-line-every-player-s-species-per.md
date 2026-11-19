---
id: BUG-1270
title: WP-0: the species line: every player's species per stage kept by the mod (table, messages, store, coop.species)
status: fixed
kind: feature
stage: All
reported: 2026-11-18
source: plan-0.7.16
role: 
versions: 
fixed_in: unreleased
branch: feat/w54-line, feat/w54-shims
commits: c6a3ec33 48b7b4d3 a2daaa0e 696d51e2 48dd3e02 fc2cc5e3
tests: species_line, unit-cpp species_tests
signature:
docs: docs/01-architecture.md#arch-species-line, docs/06-testing.md#testing-species-line
related:
---

## Report
Plan item WP-0 of docs/plan-0.7.16.md (Item 4): every player's species line per stage (creature, tribe outfit, Civ
outfit, Civ designs) kept by the mod, sent to every game, kept across rejoins and sessions in the host's per-player
store, never in the game's save. The API the other 0.7.16 packages read.

## Root cause
A feature, not a bug: the game carries only the host's line (the tribe's slot, the civilization's species, one culture
set: docs/re/civ.md#civ-own-species, docs/re/tribe.md#tribe-two-species).

## Fix
`mods/coop/species_line.h` (API: key(pid, Stage), design(pid, id), own_mode(), own_key(), setters, on_change),
`species_line_core.h` (portable rules, wire / store format), `species_line.cpp` (table, MSG_SPECIES_LINE 73, store
record species.line v1, `coop.species`), `creations::line_pack` / `line_register` (bundles registered under a shared
0x7B...... key from the content on every game). Family creations 1.1. Switch `coop.fix species.line off` (no line).
docs/01-architecture.md#arch-species-line.

## Verification
Unit: tests/species_tests.cpp (make test, unit-cpp) passes. Lab: `species_line` (lane 9) and its twin
`SPORE_FIX_OFF=species.line` are written but NOT run yet: the owner stopped all lab runs on 2026-11-18 until the night
run (to be filled in with the PASS / FAIL run dirs).
- no-fail-run: lab runs stopped by the owner (2026-11-18) until the night run; `species_line` (PASS expected) and
  `SPORE_FIX_OFF=species.line` (FAIL expected) are queued for it.
* Lab (fix/w57-savestrip, lane 4): `species_line` PASS lab 20261119-112418 and 20261119-123654 (merged tree) (step 4 with `coop.settings force
  creature.mode`: the night run's FAIL in 20261119-095414 was the test's `set`, refused since creature.mode is a session
  setting); FAIL twin `SPORE_FIX_OFF=species.line` lab 20261119-120449 (no line on the host from step 1). BUG-1315 adds
  to this package: `species::is_line_key` remembers every key a line had in this process, `set_world_species`.

## History
- 2026-11-18: opened.
- 2026-11-18: feat/w54-line: API, core, unit tests, message, store, console, lab test written (lab not run).
- 2026-11-18: feat/w54-shims: Cell keys of the line also take a premade cell every game has (not the host's: creations::player_orig), unit tests in species_tests (creation_counts); line_pack reads the owner's own My Creations first; the four packages read the line (docs/01-architecture.md#arch-species-line-packages); creations 1.1 lock rebased (no wire change).
- 2026-11-19: fix/w57-savestrip: test step 4 uses `coop.settings force`; the save filter's species side (BUG-1315).
