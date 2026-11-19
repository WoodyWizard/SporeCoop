---
id: BUG-1321
title: Civ: on the guest's own game the citizens of its city are drawn white (no paint) while the host's game paints them
status: open
kind: bug
stage: Civ
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in:
branch:
commits:
tests: civ_own_species
signature:
docs:
related: BUG-1273, BUG-1270
---

## Report
Key frames of civ_own_species (all its checks passed): lab 20261119-114816 frames 02 / 03 (guest_citizen1/2, the same
camera on both games): the host draws the guest's city citizens as the guest's creature with its paint (purple hat,
white dress, green trim); the guest's own game draws the same citizens plain white / grey (no paint). In lab
20261119-095414 frame 02 it was the other way round (the host's citizens white). The species key is the same on both
games (7B635F66, the guest's Civ outfit's shared copy); only the paint is missing.
To reproduce: `SPORE_DETECTOR=20 python3 tools/lab.py run civ_own_species --lane N`, then
`tools/frames_review.py <run> --test civ_own_species`.

## Root cause
Not analysed (found by fix/w57-savestrip, outside its save task). ~ Inferred: the paint of a line creation is baked
per game when its profile is first used; a citizen pool made before the bake finished (or with the bundle's paint
records missing on that game) keeps the white model. The test checks keys only (+0xB28), not the drawn paint.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened (fix/w57-savestrip, seen in the key frames while fixing BUG-1273's save check); not fixed there.
