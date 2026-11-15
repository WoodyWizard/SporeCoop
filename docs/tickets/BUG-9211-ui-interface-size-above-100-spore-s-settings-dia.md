---
id: BUG-9211
title: UI: Interface Size above 100 %: Spore's Settings dialog opened again is laid out wrong (the quality table, the resolution list, Defaults)
status: fixed
kind: bug
stage: all
reported: 2026-11-15
source: owner play test 2026-11-15 (night)
role: 
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 95d79b56 84936988
tests: tools/qa_win (manual, 2026-11-15): msi9211b (MSI 1280 x 960, Settings opened, Accept, opened again)
signature:
docs:
related: BUG-9191
---

## Report
Owner (screenshot, 150 %): "After increasing the interface size, and testing it. Upon re-entering the options menu, it looks very messed up." The Graphic page: the quality table has no radio buttons, the resolution list hangs below the dialog over the screen, Defaults sits outside the dialog. Also: "the resolution / quality of the menus and art are very low quality" (the art is drawn larger than its pixels).

## Root cause
Not analysed yet. Likely: the Settings layout of decision 3I (ui.settings_layout) moves and grows Spore's own boxes with set_area in screen pixels when the page opens; under the whole-UI scale (3X A) the dialog is laid out for the smaller screen, so those positions are wrong; the resolution list popup is placed by the game at its 100 % spot. The blur is the art (fixed-size images) drawn at 150 %: not fixable without larger art.

## Fix
- The Interface Size is capped to what the screen holds (min of width/1024 and height/768) and only the direct children of re-laid holders are revalidated (95d79b56).
- The rearranged Settings page (3I layout) is off by default in 54 (ui.settings_layout): after Accept it drew Spore's Options menu over the Settings, hiding the quality table's buttons and Defaults. The Borderless button is found by its size, so the original layout shows "Borderless: Off/On" (84936988).

## Verification
FAIL (MSI, 3I layout on, 100 %): after Accept the reopened Settings showed the Options buttons as pills over it; Borderless had no caption.
PASS (MSI, 84936988): first and second opening both show the radio buttons, Defaults and "Borderless: Off".
no-fail-run: Windows QA by hand (no lab on this PC).

## History
- 2026-11-15: opened.
- 2026-11-15 (night): opened from the owner's play test (build 87d69a05 on both PCs).
- 2026-11-15 (overnight): 3I layout off by default for 54, Borderless caption fixed (84936988), checked on the MSI.
