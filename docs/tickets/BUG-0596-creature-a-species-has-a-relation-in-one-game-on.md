---
id: BUG-0596
title: Creature: a species has a relation in one game only (detector state:species_only, campaigns and long_creature)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-05
source: detector, rel/0.7.7 final check out/lab/20261105-015320 and 015350
role: both
versions: 0.7.7-dev
fixed_in: 0.7.8
branch: fix/w44-world-state2
commits: d21a18ff be1f1b6b c43d2217 3567b106
tests: make test (tools_selftest.py worldstate), long_creature_short
signature:
docs: docs/re/creature.md#creature-species-only
related:
---

## Report
campaign_ct, campaign_short, long_creature (3 samples, 4 objects): "1 species with a relation in one game only
(2915F023/2B978C46/40626200 guest)". The guest's own species (its creation) may have a relation row only in one
game. Reproduce: `SPORE_DETECTOR=20 tools/lab.py run long_creature_short`.

## Root cause
Detector reads, not a lasting difference of the games' tables (social.cpp shares the table: the host sends it on a
change, a guest writes the host's records and zeroes its own others every 0.5 s).
* Observed (campaign_ct, rel/0.7.7 lab 20261105-015350): the guest was rejoining; its census listed 0 objects while
  the host's listed 150, its species table was not the host's yet ("2 species ... (host)", with 18 other rows of the
  same sample: no sky, 102 nests host only).
* Inferred: the other 4 samples (campaign_short x3, long_creature) were single ones, never two in a row: a record
  the host's game had just made (on its way: the host checks its table once a second) or the guest's own new record
  (zeroed within 0.5 s) between the two dumps. Not reproduced in a lab run.

## Fix
tools/tests/worlddiff.py: a sample where one game lists no object and the other at least 10 is skipped as `apart`
(the game loads its world); in Creature a guest's state is read again 1.5 s after the host's (SPORE_DETECTOR_BRACKET=0:
once) and a species counts as in one game only when it is so in both reads. No game change, no switch.

## Verification
make test: tools_selftest.py worldstate ("a guest that lists no object (loading): not compared", "a record on its way
to the guest: no species_only row", "a record the guest still lacks 1.5 s later: species_only"): the first two FAIL
against the rel/0.7.8 worlddiff.py (SELFTEST_WORLDDIFF), PASS here. Detector: long_creature_short with
SPORE_DETECTOR=20 out/lab/20261105-123555: 34 Creature comparisons, no Creature:state:species_only (before: the
0.7.7 final check, 4 samples in campaign_ct / campaign_short / long_creature).

## History
- 2026-11-05: opened.
- 2026-11-05: fixed in the detector (fix/w44-world-state2).
- 2026-11-05: released in 0.7.8.
