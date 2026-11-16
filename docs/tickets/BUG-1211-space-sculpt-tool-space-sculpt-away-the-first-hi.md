---
id: BUG-1211
title: space_sculpt_tool / space_sculpt_away: the first hit falls on a plateau since the faster start (the hits change the ground by 0-0.5 m)
status: open
kind: test
stage: Space
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: unreleased
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
0.7.15 final check (lab 20261116-025154): space_sculpt_tool "the host's second hit changed its terrain at the spot
(599.51 -> 599.48)" and space_sculpt_away "(599.51 -> 599.28)"; the same on rel/0.7.15 on lane 6 (lab
20261116-082811: 599.46 -> 599.37, 599.96 -> 599.74). v0.7.14.1 on the same lane passed both (lab 20261116-084330:
577.13 -> 587.09, 595.24 -> 599.65).

## Root cause
* Observed: the first hit's spot (`coop.space sculpt` last=) was x = -2 .. -85 in every run up to 0.7.14.1 (ground
  577-597 m there) and x = +45 .. +116 in every 0.7.15 run (ground 599.3-599.96 m). The first sculpting tool raises the
  ground to about 599.5-599.8 m (every passing run ended there, from 594-597; from 577 it rose by 10 m), so a hit on
  ground already at that height changes it by 0.0-0.5 m: the check (> 0.5 m) failed.
* Observed: the host landed 21 s after the world opened on 0.7.15 (lab 20261116-085402) and 33 s on 0.7.14.1 (lab
  20261116-084330): its ship came down at another point of the turning planet ((187, -517, 1388) against (-272, -474,
  1380)), and the hits go to a fixed screen point under the landed ship. The earlier landing is the faster start of
  0.7.15 (A9, an intended change), not a change of the sculpting or its sync: with the autosave fix off the test
  still passed when the spot happened to have room (lab 20261116-085402, 599.34 -> 599.89).

## Fix
Test only: after the landing both ships go over a fixed point of the planet (`coop.space over -5 -176 557`, the ground
about 594 m there, below the tool's height) before the hits. The checks are unchanged.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
