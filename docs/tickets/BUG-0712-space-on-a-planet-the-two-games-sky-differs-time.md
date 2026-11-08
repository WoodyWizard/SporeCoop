---
id: BUG-0712
title: Space: on a planet the two games' sky differs (time of day 160 deg apart)
status: fixed
kind: desync
stage: Space
reported: 2026-11-08
source: fix/w46-k56-space, lab 20261108-020511 detector
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k8-open
commits: 5ca765b6
tests: space_sky, wsync_transitions
signature:
docs: docs/re/space.md#space-planet-sky
related: BUG-0579
---

## Report
Seen by the divergence detector during coop_space_shots.py (lab 20261108-020511, lane 9, fixture space_empire): with
both players' ships on the colony planet 01460035, every sample had `All:state:time_of_day` "the sky 160 deg apart
(host q=(0.02 -0.05 0.48 0.87), guest q=(0.03 -0.07 0.94 -0.33))". BUG-0579 synchronises the time of day only while a
guest has an avatar (Creature / Tribe), so a Space planet visit is not covered (inferred). Reproduce: run
coop_space_shots.py with SPORE_DETECTOR=20 and read the DETECTOR lines (or `tools/detector_report.py <run>`).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (space_sky, fix off, lab 20261108-030244): both ships on the colony planet 01460035, each game's own sky
(TimeOfDay_GetObject +0x10) 166.5 deg apart in every sample of 30 s. Cause (code, wsync.cpp): the sky row's stage gate
`sky()` was Creature / Tribe / Civ only (BUG-0579), and MSG_WORLD_TIME is ignored by a guest with that row on (and
needed an avatar): in Space nothing brought the guest's planet sky to the host's.

## Fix
`wsync.space_tod`: the sky row is in play in Space while the players are on the same planet (wsync::Ctx::space_sky:
host on a planet; guest on a planet and the host's latest state at the same place). The day length row keeps its
old stages. `coop.wsync sky` prints this game's sky in any stage (tests). Off: 0.7.10.

## Verification
space_sky (coop_space_sky.py: both land on the colony planet, each game's sky every 3 s for 30 s; PASS within 0.1 rad
in the last 15 s):
- FAIL with SPORE_FIX_OFF=wsync.space_tod: out/lab/20261108-030244 (166.5 deg in all samples), lane 4.
- PASS: out/lab/20261108-030244-2 (x2), lane 5.

## History
- 2026-11-08: opened.
- 2026-11-08: fixed on fix/w46-k8-open (wsync.space_tod).
