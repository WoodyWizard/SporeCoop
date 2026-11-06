---
id: BUG-0605
title: Detector: coop.wstate faults on freed civilizations while the game changes from Civ to Space (crash in campaign_space)
status: fixed
kind: crash
stage: Civilization
reported: 2026-11-05
source: lab rel/0.7.8 20261105-134715 campaign_space (detector run)
role: guest
versions: 0.7.8-dev
fixed_in: 0.7.8
branch: fix/w44-wstate-transition
commits: 63598921
tests: campaign_space
signature:
docs:
related:
---

## Report
campaign_space with SPORE_DETECTOR=20 (rel/0.7.8, out/lab/20261105-134715): instance 26 crashed (0xC0000005,
a call through a vtable read as ASCII 0x72656472) while the campaign went from Civilization to Space; the
campaign stopped in Civ. Chain (stack scan, build of that run): census::pol <- census::c_wstate (wstate.cpp, the
`civ pol=` rows), i.e. the detector's `coop.wstate` command. Only test runs call it; players never do.

## Root cause
Observed: c_wstate takes the mode from the mod's frame (WC->place = g_mode), which lags the game's during a stage
change; the civilization list it walks (and calls vt 0x4C on) holds freed objects then. Same class as BUG-0271
(local_now): anything read between frames must check the game's mode and the stage leave.

## Fix
`census.wstate_live` (wstate.cpp): coop.wstate reads nothing (prints `st phase changing=1`) while the game's mode
is not the frame's or the stage is being left. `coop.fix census.wstate_live off`: as before.

## Verification
no-fail-run: a race of one stage change (the detector's read inside the Civ -> Space teardown); the guard is the
same check local_now uses. Regression: campaign_space with SPORE_DETECTOR=20 in the next rel/0.7.8 integration run.

## History
- 2026-11-05: opened.
- 2026-11-05: released in 0.7.8.
