---
id: BUG-0854
title: Saves and the quit message are allowed in an adventure: in a standalone adventure they crash at cStarManager::GetPlanetRecord+0x2D
status: open
kind: crash
stage: Space
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9004
role: both
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: fork: cStarManager::GetPlanetRecord+0x2D (null home planet)
docs: docs/re/space.md#space-fork-adventure-notes
related: 
---

## Report
In an adventure started from the galaxy menu (Galactic Adventures, no campaign home planet), the mod's save or its
quit-to-menu message crashes the game; the fork reproduced it with `coop.menu` on both games. Which player paths
reach it in our code: mostly the test commands coop.save and coop.menu; check also queued loads and shares.

## Root cause
* Observed (code): save_blocker (coop.cpp:1873) explicitly allows kScenario; world.cpp:940 post_quit_to_menu has no
  Scenario guard; debug.cpp:92 coop.menu posts 0x4715068 directly.
* Observed: shares are not made in Scenario (the Adventure adapter has own_worlds = true), and a queued load from an
  adventure goes through load_now, not the quit message (world.cpp:989 `other`).
* Inferred (fork): the campaign save path needs a home planet and reads null in a standalone adventure. Confidence
  high for the dev commands, low for a player path.

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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
