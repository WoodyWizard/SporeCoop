---
id: BUG-0938
title: Civ: the galaxy menu's logo and legal lines stay on a guest's screen during play after a join from the menu (campaign_space)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-090547
role: guest
versions: 0.7.12 (unreleased)
fixed_in: unreleased
branch: fix/w48-rest
commits: d5d96134
tests: menu_splash, creature_intro_camera
signature:
docs: docs/re/creature.md#creature-menu-splash
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
Proven (2026-11-12, fix/w48-rest; Ghidra and lab lanes 4 / 5, docs/re/creature.md#creature-menu-splash):
* The texts are the galaxy menu's splash, not its intro cinematic: GalaxyGameEntry ([0x16A1344]) +0x204 holds a table
  of 6 steps {window id, delay, time shown}: 0x0670A3F8 (0.1 s), **0x05B82B38 the legal lines (logo "galactic
  adventures", "Online Interactions Not Rated by the ESRB", "(c) 2009 Electronic Arts Inc.", 4.1 s)**, 0x0670A3F8,
  0x05BFDC38, 0x05BFDC68, 0x066F3C30. The splash update 0xDEAC80 shows a step's window and hides it when its time is
  up; 0xDEA510 ends the splash (hides 0x06776FE0 / 0x06776FE1, deletes the layout). Both run only in the menu.
* Leaving the menu does not hide a step that is up: with the legal lines up (put up as their step does, the game's
  SetFlag(visible) on 0x05B82B38) and the join right after, the lines stood over the Creature play for the whole test
  (key frame of out/lab/20261112-140450: the same logo, ESRB and copyright lines as Carol's frame). Carol joined 11 s
  after her menu came up, inside the splash; Dave's join came at another step.
* Not BUG-0626's hook: the intro cinematic and its "not started" line are another part of the menu (the guests in the
  lab reach their menu after the splash: state 2, the legal lines never drawn in 28 s of polling).

## Fix
mods/coop/world.cpp `menu_splash_tick` / `hide_menu_splash`: once a second in a stage (not the menu, not a load),
outside the frame, every window of the game's own splash table (and 0x06776FE0 / 0x06776FE1) that is drawn (visible
with every parent) is hidden as its step's end hides it. The menu shows them again itself on the next visit.
`coop.status`: "menu splash hidden N". Switch: `coop.fix world.menu_splash off`.

## Verification
Test `menu_splash` (tools/tests/coop_menu_splash.py, creature fixture, cold): the guest waits for its menu, the legal
lines are put up, it joins at once; in the stage three times over 20 s the guest's own window tree: no splash window
drawn (`coop.ui where` through visible parents), the host neither; key frame `stage`.
FAIL (SPORE_FIX_OFF=world.menu_splash): out/lab/20261112-140450 (0x05B82B38 and 0x06776FE0 drawn at 3, 10, 17 s; the
key frame shows the logo and the ESRB lines over the creature).
PASS: out/lab/20261112-140452, PASS2_0938 (hidden 0.4 s into the stage, the frame clean).

## History
- 2026-11-11: opened.
- 2026-11-12: the menu splash named (GalaxyGameEntry +0x204), a step left up by a join from the menu hidden in the stage (world.menu_splash), fix/w48-rest.
