---
id: BUG-0712
title: Space: on a planet the two games' sky differs (time of day 160 deg apart)
status: open
kind: desync
stage: Space
reported: 2026-11-08
source: fix/w46-k56-space, lab 20261108-020511 detector
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
Seen by the divergence detector during coop_space_shots.py (lab 20261108-020511, lane 9, fixture space_empire): with
both players' ships on the colony planet 01460035, every sample had `All:state:time_of_day` "the sky 160 deg apart
(host q=(0.02 -0.05 0.48 0.87), guest q=(0.03 -0.07 0.94 -0.33))". BUG-0579 synchronises the time of day only while a
guest has an avatar (Creature / Tribe), so a Space planet visit is not covered (inferred). Reproduce: run
coop_space_shots.py with SPORE_DETECTOR=20 and read the DETECTOR lines (or `tools/detector_report.py <run>`).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
