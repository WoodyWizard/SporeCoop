---
id: BUG-0747
title: Space: an NPC empire's store pays a different price for the same spice in each game
status: open
kind: desync
stage: Space
reported: 2026-11-08
source: fix/w46-g4b-coverage, coop_space_store.py lane 3 2026-11-08
role: 
versions: 0.7.10 and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/space.md#space-npc-store-runtime
related: BUG-0199
---

## Report
Seen by `coop_space_store.py` (lane 3, 2026-11-08, `space_empire`): in the same session the guest sold one Red Spice
to the store of the empire 80001FCC (home star 0046102D) for 281, the host one minute later to the same empire's store
for 4,779; in an earlier session the host's store showed 6,747 for it. Each game makes its own store
(GenerateNPCStore 0x103E8F0 picks the 3 wanted spices and a price factor at random, docs/re/space.md 14.2), so two
players at the same empire see different wants and prices: one sells for the full price, the other for 5%. The shared
money hides it (both earnings land in one budget) but the trade screens disagree.

Reproduce: `SPORE_LANE=N python3 tools/tests/coop_space_store.py` and compare the two "the store paid" lines.

## Root cause
Inferred: the store is generated in each game separately, from the game's own RNG, when that game first needs it
(not traced at runtime which call). Not analysed further on fix/w46-g4b-coverage (a coverage task).

## Fix
Not fixed. Idea: the host's store (wanted keys +0x60, prices +0x9C) sent to the guests as world state, or the
generation seeded from the empire id on every game.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-g4b-coverage): seen by space_store (guest 281, host 4779 for the same spice and empire). Open.
