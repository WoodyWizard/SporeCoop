---
id: BUG-9211
title: UI: Interface Size above 100 %: Spore's Settings dialog opened again is laid out wrong (the quality table, the resolution list, Defaults)
status: open
kind: bug
stage: all
reported: 2026-11-15
source: owner play test 2026-11-15 (night)
role: 
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs:
related: BUG-9191
---

## Report
Owner (screenshot, 150 %): "After increasing the interface size, and testing it. Upon re-entering the options menu, it looks very messed up." The Graphic page: the quality table has no radio buttons, the resolution list hangs below the dialog over the screen, Defaults sits outside the dialog. Also: "the resolution / quality of the menus and art are very low quality" (the art is drawn larger than its pixels).

## Root cause
Not analysed yet. Likely: the Settings layout of decision 3I (ui.settings_layout) moves and grows Spore's own boxes with set_area in screen pixels when the page opens; under the whole-UI scale (3X A) the dialog is laid out for the smaller screen, so those positions are wrong; the resolution list popup is placed by the game at its 100 % spot. The blur is the art (fixed-size images) drawn at 150 %: not fixable without larger art.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
- 2026-11-15 (night): opened from the owner's play test (build 87d69a05 on both PCs).
