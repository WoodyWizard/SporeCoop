---
id: BUG-0938
title: Civ: the galaxy menu's logo and legal lines stay on a guest's screen during play after a join from the menu (campaign_space)
status: open
kind: bug
stage: Civ
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-090547
role: guest
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0626, BUG-0607
---

## Report
Key-frame review of the second final check out/lab/20261111-090547 (rel/0.7.12 94ca59ab).
Sheet: `out/lab/20261111-090547/frames/sheets/020_campaign_space_01_play-civ.jpg` (the third tile). Frame: `out/lab/20261111-090547/frames/campaign_space/01_play-civ_guest-Carol.jpg`
(campaign_space, lane 5, seed 577009384, result ok).

* Guest Carol, end of 2 min of Civ play (09:21:25): over the lower left of the picture stand the logo "galactic
  adventures", the line "Online Interactions Not Rated by the ESRB" and "(c) 2009 Electronic Arts Inc. All Rights
  Reserved.": the texts of the galaxy (main) menu. The Civ HUD, the minimap, the co-op status and the partner
  arrows are drawn normally with it.
* The host and the guests Bob and Dave do not have it. Carol's next frame (Space, 02_advance-civ) does not have it.
* Carol joined from the galaxy menu 11 s after the menu came up (its log: "stage: Galaxy menu -> Loading" at
  09:19:17, "the galaxy menu intro would start outside the menu (mode 01654C04): not started (BUG-0626)" at
  09:19:22). Dave took the same path and has the same log line but no logo.
* Not in the first final check (20261111-044257: Carol had a city planner open at this frame, which would hide it).

Severity: low to medium (a menu logo over play until the next stage change; a player who joins within about
16 s of reaching the menu). Confidence: high that it was on the screen, seen once (1 of 6 guests that joined from
the menu in Civ or later in this run; the other campaign frames do not show it). Regression of 0.7.12: unknown.

## Root cause
Not analysed. Inferred (medium): the menu's intro sequence shows these texts by its own timer about 16 s after
the menu came up (docs/re/tribe.md#tribe-intro-camera: "GGE_intro" starts ~16 s after the menu even when the
quick load left it). world.intro_at_once (BUG-0626) keeps the cinematic from starting outside the menu, but the
text layer of the menu is shown by another path and nothing hides it in a stage. Why only one of two guests with
the same log line: not known (a difference of a frame between the timer and the mode change).

## Fix
Open.

## Verification
Open. How to test: a guest that joins 8 to 14 s after its galaxy menu came up (the lab's quick join), then
`coop.ui find "ESRB"` (or the logo window's visible flag) 30 s into the stage on every game: must find nothing.
Repeat 10 times: the frame above is 1 of 2 in this run. The key-frame review would catch it only by chance.

## History
- 2026-11-11: opened.
