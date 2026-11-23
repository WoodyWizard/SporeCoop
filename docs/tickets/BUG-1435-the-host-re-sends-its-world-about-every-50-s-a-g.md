---
id: BUG-1435
title: The host re-sends its world about every 50 s: a guest passing through the galaxy menu to load the world it was just sent counts as left to the main menu after 5 s
status: open
kind: bug
stage: Creature
reported: 2026-11-22
source: discord
role: all
versions: 0.7.15.5
fixed_in:
branch: fix/w64-share
commits: aa1d7d26, f6f56ea1
tests: share_menu_pass (coop_share_menu_pass.py); twin share_menu_pass_off
signature:
docs: docs/07-modes.md, docs/06-testing.md#testing-share-menu-pass
related: BUG-0508, BUG-1434, BUG-0231
---

## Report
The host re-sent its world 8 times, about every 53 s (the players saw 'it constantly reloads the world'). Full analysis: out/research/dc-report-20261122-world-switch.md in the main checkout (not in git; six SporeCoop-report zips, coop 0.7.15.5, Steam).

## Root cause
Confirmed on the session clock in 7 rounds: during the ~40 s transfer the guest keeps playing the stage, so joined_phase becomes true; then the guest goes through the galaxy menu to load the world (5.2-5.4 s), and the host's 5.0 s left-to-menu window (session_glue.cpp:444 and :461, since 2026-10-23) fires, so the host sends the world again. Not fixed in rel/0.7.16.

## Fix
Every share (any caller: the join's, a phase's, coop.share, the BUG-0508 fallback) bumps `world::share_serial(peer)`. follow_phase (session_glue.cpp `load_wait`) then waits for that guest's load: while the share is in flight its frames in the old copy do not set joined_phase; after it went out, its pass through the galaxy menu / loading screen is not "left to the main menu". The wait ends when the guest is back in the host's mode after passing the menu or loading screen (loaded), 30 s after it entered them (a guest that really went to the main menu gets the world 5 s later, as before), or 120 s after the share went out without the guest leaving its game (auto_load_world=0, a refused load). Switch `coop.fix core.share_load_wait off`: the old 5 s window (0.7.16). Test hook `coop.worldfix menusettle <s>` (the guest's menu settle before the load, default 3 s). No message changed; the host's `coop.status` shows `load_wait <id> ...`.

## Verification
Open, waiting for the lab run. PASS expected: share_menu_pass (the guest rejoins with a 6 s menu settle: over 150 s the guest's game receives and loads the world once, passes the galaxy menu once, the host sends once, both play together at the end). FAIL expected: share_menu_pass_off (SPORE_FIX_OFF=core.share_load_wait): the world goes out again while the guest is in the menu, again and again.

## History
- 2026-11-22: opened.
- 2026-11-22: fixed on fix/w64-share (wait for the guest's load of the world sent last), test and twin, waiting for the lab run.
