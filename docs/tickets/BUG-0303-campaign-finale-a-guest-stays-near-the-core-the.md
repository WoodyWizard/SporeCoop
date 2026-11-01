---
id: BUG-0303
title: campaign finale: a guest stays near the core, the Space bot had opened the captain screen (the wheel zoom-out is blocked)
status: fixed
kind: tooling
stage: space
reported: 2026-10-30
source: lab 20261030-075052 campaign#3
role: 
versions: 0.7.3.1
fixed_in: 0.7.3.2
branch: fix/w11-finale
commits: 24a9a55
tests: space_captain, campaign (finale)
signature:
docs: docs/re/space.md#space-captain-screen-fault, docs/06-testing.md
related: BUG-0304, BUG-0277
---

## Report
Lab 20261030-075052 campaign#3 (main 390f73d, lane 7, 4 players): in the finale the extra guest
Carol pressed "Fly to Alice" (near the core) and arrived in the system next to the core, then
"zoomed out with 10 x 5 notches" but stayed in the solar system (`FINDING [stuck] finale: Carol:
zoomed out to the galaxy view`); her second "Fly to Alice" only repeated "The galactic core is
close: zoom out to the galaxy view", so "every player entered the galactic core" failed with Carol
at zone=2 dist=61.8 star=9310217 (Alice, Bob and Dave at the core, zone 3). The same campaign passed
on lanes 2 and 4.

## Root cause
Observed: Carol's screenshots at the fault finding (Space play, ~10 min before the finale) and at
the stuck finding both show the captain screen open; the game ignores the mouse wheel while it is up
and it is no caption (busy=0), so neither `settle()` nor the wheel loop of `to_galaxy()` saw it.
Reproduced on lane 2: a click on the rank badge (1390 890) opens it; 3 x 5 wheel notches leave the
context at 1; `space_captain` with the old tool fails the same way (docs/re/space.md#space-captain-screen-fault).
Inferred: the Space play bot (play_space.Player) clicked a planet or star whose projected point lay
under the badge: its target filter allowed x < W-40, y < H-180, and the badge reaches up to H-205.
A test-tool problem: a player closes the screen before zooming out. Opening it also costs the game's
own caught fault (BUG-0304), which was the campaign's [fault] finding.

## Fix
Test tooling (24a9a55): play_space.Player.targets skips points under the badge (x > W-120 and
y > H-225); `play_space.captain_open` / `close_captain` (OK button; Esc would open the pause menu);
play_space's captions() closes a captain screen like the pause menu; coop_space_finale.to_galaxy
closes it before each wheel step. New test coop_space_captain.py (suite `space_captain`).
switch: OLD_FINALE_TOOL=1 (environment) keeps the old to_galaxy (the FAIL run).

## Verification
FAIL: lab 20261030-095947-fix-w11-finale (space_captain, OLD_FINALE_TOOL=1: "zoomed out with 10 x 5
notches", ctx=1, the screen still open). PASS: lab 20261030-100128-fix-w11-finale,
20261030-100242-fix-w11-finale (space_captain). Not re-run: the 4-player campaign (150 min).

## History
- 2026-10-30: opened; reproduced and fixed (test tooling) on fix/w11-finale.
- 2026-11-01: released in 0.7.3.2.
