---
id: BUG-9358
title: Galaxy menu: Play on a server planet can enter another planet when the list changes during Play
status: fixed
kind: bug
stage: galaxy
reported: 2026-11-23
source: qa
role: 
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 7f3d2bd0
tests: tools/qa_win (manual, the MSI, a real Play click)
signature:
docs: docs/12-server-and-players.md#server-menu-gaps
related: BUG-9287, BUG-9290, BUG-9295, BUG-9337
---

## Report
The MSI, 2026-11-23: Play on Testplanet01 in the galaxy menu entered it, then a second later left it and entered
Samuth (galaxy log: "Testplanet01: its host left", "Samuth: hosts it").

## Root cause
The Play step reads its entry's name every frame until the menu is left. Entering a planet gives it a player, the
list's order changes (planets with players first), the menu's entries are rewritten during the Play state, and the
entry Play holds names another planet, which was entered next. Seen in the log; read in the code.

## Fix
The menu's entries are rebuilt only while it shows the planets (state 2), and a Play takes no other planet for 30 s
after it entered one. Switch `coop.fix galaxy.play_hold off`: as before.

## Verification
FAIL (the build before, 19:39, the MSI): Testplanet01 then Samuth. PASS (7f3d2bd0's code, 19:44): the same real Play
stayed on Testplanet01 (galaxy log: hosted by the player, no Samuth).
no-fail-run: the FAIL run is the build without the fix.

## History
- 2026-11-23: opened.
- 2026-11-23: fixed on feat/lineages-alpha (7f3d2bd0), FAIL and PASS seen on the MSI.
