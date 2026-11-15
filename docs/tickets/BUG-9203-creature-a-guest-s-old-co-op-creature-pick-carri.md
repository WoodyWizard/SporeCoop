---
id: BUG-9203
title: Creature: a guest's old co-op creature pick carries into a brand-new world, so the guest never gets the creature editor
status: open
kind: bug
stage: Creature
reported: 2026-11-14
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9164 BUG-0344 BUG-0345 BUG-9159 BUG-9170
---

## Report
Owner play test 2026-11-14 (3 players, the host started a new Creature world from the galaxy menu with two guests waiting): the guests were not put into the creature editor although they had no creature on that world; each played a creature from its own collection.

## Root cause
Observed (host and guest logs): each guest used its co-op window pick saved in choices.ini (guest log: `own=... 'Duckulon' from choices.ini`; host: "plays its own creature"). Inferred from code: BUG-0345 makes a co-op window pick hold everywhere (`creations::mine()`, creations.cpp around 1468-1500); the host already drops a pick made in another world (BUG-0344, `!guest`), guests do not. BUG-9164's `join_editor_tick` (coop.cpp around 2466-2502, switch creature.join_editor_first) skips a guest whose `plays_own_creature()` is true, so the old pick blocks the editor. Side note: during the host's pre-game editor the guest status line says the host "is evolving the species in the editor" (coop.cpp around 6686), which is wrong for a new world.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-14: opened.
