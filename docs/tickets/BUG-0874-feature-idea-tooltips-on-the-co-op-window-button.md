---
id: BUG-0874
title: Feature idea: tooltips on the co-op window buttons in Spore's own style
status: fixed
kind: feature
stage: All
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages42-dev, BUG-9107 (coop_ui.cpp)
role: both
versions: 0.8.0
fixed_in: 0.8.0
branch: w08/lobby-qol
commits: 80e954c7 c8149c8b f61197e6
tests: ui_tooltips, ui_tooltips_off
signature:
docs: docs/08-ui.md#ui-lobby-qol
related: BUG-0867
---

## Report
The co-op window's buttons have no explanation; the fork shows short tooltips in Spore's native style after a
0.8 s rest.

## Root cause
Not a bug, a UI feature.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages lineages42-dev, BUG-9107, mods/coop/coop_ui.cpp); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* The tooltip background is Spore's own drawable class UTFWin::StdDrawable (ctor 0x988350, object 0x7C bytes, images
  at +0x10), with a 9-slice image data/ui/lineages_tooltip.png; shown after the mouse rests 0.8 s on a button,
  hidden on move / click. Text per button in English and Russian.
* Overlap: plan-0.7.12 F1 (co-op window 2.0); add the tooltips to the new window.

## How to test
UI test by real mouse moves: rest 1 s over Host, the tooltip window is visible with the text; move away, hidden;
no tooltip before 0.8 s.

## Verification
- Switch: `ui.tooltips`
- Symbols: `UTFWin::StdDrawable::StdDrawable=0x00988350` verified via `objdump` on GOG 2024 `dc04aee5` (`0x7C` bytes, `images[8]` at `+0x10..+0x2C`, 9-slice scale type `1` and `1/3` borders at `+0x30..+0x40`).
- Wine lab (lane 4, run `20261125-030621`, commit `f61197e6`):
  - `ui_tooltips`: PASS (25 s, 0 faults on host & guest): no tooltip at 0.25 s (< 0.8 s), EN tooltip visible over Host button after 1.0 s (`"Start hosting on this UDP port"`), hidden immediately on mouse move away, RU tooltip visible after 1.0 s (`"Открыть сервер на этом UDP-порту"`), hidden on move away; visual verification on `ui_tooltip_host_en.png` and `ui_tooltip_host_ru.png`.
  - `ui_tooltips_off` (`SPORE_FIX_OFF=ui.tooltips`): FAIL (29 s, both EN and RU tooltip visibility checks fail).

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
- 2026-11-25: implemented in WP-Q (`w08/lobby-qol`) with native `UTFWin::StdDrawable` 9-slice `data/ui/tooltip_bg.png` overlay, `0.8 s` hover rest timer via `mouse::last_move` / `mouse::clicks()`, EN + RU button tooltips, and `coop.ui state` inspection; verified ON PASS and OFF FAIL in lab run `20261125-030621`.
- 2026-11-25: released in 0.8.0.
