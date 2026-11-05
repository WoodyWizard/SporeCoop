---
id: BUG-0596
title: Creature: a species has a relation in one game only (detector state:species_only, campaigns and long_creature)
status: open
kind: desync
stage: Creature
reported: 2026-11-05
source: detector, rel/0.7.7 final check out/lab/20261105-015320 and 015350
role: both
versions: 0.7.7-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
campaign_ct, campaign_short, long_creature (3 samples, 4 objects): "1 species with a relation in one game only
(2915F023/2B978C46/40626200 guest)". The guest's own species (its creation) may have a relation row only in one
game. Reproduce: `SPORE_DETECTOR=20 tools/lab.py run long_creature_short`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-05: opened.
