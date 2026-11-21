---
id: BUG-9304
title: Saves: the campaign autosave pauses both games and shows the saved box
status: open
kind: feature
stage: all
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: host
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0332, BUG-9260
---

## Report
Live 67-dev: "Autosave isn't seamless still, this may have been solved on Alpha, but not on Live?"

## Root cause
Observed: every save on Live (and on alpha) is the pause menu's Save message (world.cpp ~571): a 1 s game-time pause,
the busy cursor, a ~0.7 s write and the saved box the mod clicks; guests pause with the host (BUG-9260). In Creature the
goal-card awards made 4 of them in 8 minutes (social.cpp host_goal_pay -> request_autosave). Alpha's silent save
(520ace7b) is used only for galaxy-server uploads, not for campaign autosaves. The two full reloads on entering Tribe
(BUG-0332 one ground; the founders' refresh) are a separate, structural cost (49.5 s loading screen for the guest).

## Fix
Planned: port world::silent_save (520ace7b) and use it for the campaign autosave only, switch `world.silent_autosave`;
share and checkpoint saves keep the old path. Test: an autosave with the switch off shows the box and pauses the guest
(FAIL), on: no box, no pause (PASS). Merging the two Tribe reloads into one is decision 7M.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
