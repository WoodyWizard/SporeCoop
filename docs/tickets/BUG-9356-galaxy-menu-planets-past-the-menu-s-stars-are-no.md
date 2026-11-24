---
id: BUG-9356
title: Galaxy menu: planets past the menu's stars are not shown
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
tests: tools/qa_win (manual, the MSI, 15 planets: 12 listed-only test copies)
signature:
docs: docs/12-server-and-players.md#server-menu-gaps
related: BUG-9287, BUG-9290, BUG-9295, BUG-9337
---

## Report
The galaxy menu has as many stars as the slot's saves plus the star manager's empty stars (8 or 9 with an empty slot, 6
on a fresh profile); a galaxy with more planets showed only that many: the rest could not be entered from the menu.

## Root cause
GGE_RebuildPlanets sizes the entries to the saved stars plus the list of empty stars (0xBA6450), read in Ghidra
(docs/re/space.md#space-menu-pages); the mod rewrote only the entries there were.

## Fix
With more planets than stars the last star is "More planets (k/n)" (k the page it shows next); its Play turns the
page instead of entering (the menu's state switch to 0xC is taken: back to the planets, then what the planets' Esc
does on a saved planet's panel: GGE_CameraToGalaxy and the planet set's vt 0x60(0, 1)). Switch
`coop.fix galaxy.menu_pages off`: as before.

## Verification
The MSI, 15 planets, 9 stars: switch off "9 of 15 server planet(s) in 9 entries" (6 never shown); on "8 of 15 ...
page 1 of 2", the star "More planets (2/2)", its Play by a real click: "page 2 of 2", the camera back on the whole
galaxy. Before the camera call the view stayed zoomed in and dark until Esc (seen, fixed in the same commit).
no-fail-run: the FAIL is the switch off on the same build.

## History
- 2026-11-23: opened.
- 2026-11-23: fixed on feat/lineages-alpha (7f3d2bd0), seen off and on on the MSI.
