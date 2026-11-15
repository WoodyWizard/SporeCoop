---
id: BUG-9204
title: Creature: partners are missing from each other's minimap (the host sees no guest, guests see the host but not each other)
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
related: BUG-0561 BUG-0460 BUG-0489
---

## Report
Owner play test 2026-11-14 (3 players, Creature): partners do not see each other on the map. Guests see the host, but not each other; the host sees neither guest. The owner: they should simply be discovered.

## Root cause
Not proven yet. Creature has only the game's own minimap (nests with faces and the discovered mark, docs/re/creature.md around the 0xD425A0 / 0xE10560 notes, plus the own avatar); no mod code puts partners on it (partner tags exist only on the Space galaxy map, space_map.cpp). Hypothesis (low confidence): what guests see is the host's home nest (the save's original player nest), while each seat nest made at runtime (home_nests.cpp) is marked discovered only on the game that owns it. Next: a minimap screenshot on each game to tell nests from creature dots.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-14: opened.
