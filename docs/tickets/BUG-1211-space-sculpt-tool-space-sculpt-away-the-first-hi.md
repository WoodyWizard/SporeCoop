---
id: BUG-1211
title: space_sculpt_tool / space_sculpt_away: the first hit falls on a plateau since the faster start (the hits change the ground by 0-0.5 m)
status: fixed
kind: test
stage: Space
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: unreleased
fixed_in: 0.7.15
branch: fix/w51-t3
commits: ebe05841, 4b28e516
tests: space_sculpt_tool, space_sculpt_away
signature:
docs: docs/06-testing.md#testing-space-autosave
related: BUG-0763, BUG-0900
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
FAIL (rel/0.7.15, no change): final check 20261116-025154 and out/lab/20261116-082811 (both tests, the spot at 599.5 m).
PASS v0.7.14.1: out/lab/20261116-084330. A pass with the old test on 0.7.15 only where the spot had room:
out/lab/20261116-085357, -085402.
First version of the fix (both ships over the spot): out/lab/20261116-100807 space_sculpt_tool #2 FAIL ("You must choose
a valid ground": the guest's ship stood under the click point); the guest 30 m aside (4b28e516):
PASS out/lab/20261116-102848 (lane 6, space_sculpt_tool 2 of 2, space_sculpt_away 2 of 2). no-fail-run: test change
only (the mod is unchanged).

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t3 (test only).
- 2026-11-18: released in 0.7.15.
