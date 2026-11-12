---
id: BUG-0854
title: Saves and the quit message are allowed in an adventure: in a standalone adventure they crash at cStarManager::GetPlanetRecord+0x2D
status: fixed
kind: crash
stage: Space
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9004
role: both
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in: unreleased
branch: fix/w48-saves
commits: f4b30adf
tests: adventure_save
signature: cStarManager::GetPlanetRecord+0x2D data 0 (0xBA6F3D, GOG 2024), x2 after coop.save / coop.menu in the tutorial adventure
docs: docs/re/adventure.md#adventure-mod-saves, docs/06-testing.md#testing-saves-w48
related: 
---

## Report
In an adventure started from the galaxy menu (Galactic Adventures, no campaign home planet), the mod's save or its
quit-to-menu message crashes the game; the fork reproduced it with `coop.menu` on both games. Which player paths
reach it in our code: mostly the test commands coop.save and coop.menu; check also queued loads and shares.

## Root cause
Observed (lab 20261112-023031, switch off): in the tutorial adventure started from the galaxy menu, `coop.save`
(the save message 0x1CD20F0) and `coop.menu` (the quit message 0x4715068) were posted; the host's game faulted twice at
0x00BA6F3D `Simulator::cStarManager::GetPlanetRecord+0x2D` (data 0, a null planet record: no campaign home planet),
as the fork saw. The game's exception handler kept it running. save_blocker allowed kScenario; post_quit_to_menu had no
Scenario guard; coop.menu posted the quit itself. Player paths: no share is made in an adventure and a load from one
goes through load_now; the mod's own save / quit path is reached by the commands and by anything that calls
save_game / post_quit_to_menu while an adventure runs.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9004); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

save_blocker returns "Finish or quit the adventure in its own menu before saving the campaign." for kScenario;
post_quit_to_menu (made non-static) refuses in Scenario ("Use the adventure's own menu to leave the mission.");
coop.menu calls world::post_quit_to_menu instead of posting the message itself. Their test (duo_adventure.py) checks
both refusals on each game, then leaves through Options > Exit to Main Menu, zero faults.

## How to test
Lab: start the tutorial adventure from the galaxy menu on one game, run coop.save and coop.menu. Assert: both
refuse with a message, the game keeps running, no crash report. FAIL on main (crash), PASS with the guard.

## Fix
world.cpp: save_game and post_quit_to_menu (now exported) refuse in kScenario ("finish or quit the adventure in its own
menu before saving the campaign" / "leave the adventure through its own menu"); coop.menu goes through
post_quit_to_menu. Switch `coop.fix world.adventure_saves off` = 0.7.12.

## Verification
`adventure_save` (coop_adventure_save.py, one game, cold): the tutorial adventure from the galaxy menu, coop.save,
coop.menu.
* FAIL: out/lab/20261112-023031 (`SPORE_FIX_OFF=world.adventure_saves`, lane 2): both posted, 2 faults at
  GetPlanetRecord+0x2D.
* PASS: out/lab/20261112-022831 (lane 2): both refused, the game stays in Scenario, 0 faults.
adventure_start.py had stopped working (the menu intro outlasted its 1300 frames and the menu entry ignored a click
without hover); fixed on the way.

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-12: reproduced (GetPlanetRecord+0x2D) and fixed on fix/w48-saves (world.adventure_saves), FAIL 20261112-023031, PASS 20261112-022831.
