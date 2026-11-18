---
id: BUG-9179
title: UI: the gold and red buttons look dim and grimy
status: open
kind: bug
stage: all
reported: 2026-11-13
source: owner
role: all
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, the MSI 2026-11-13: the Co-op window, a hover on Report Issue)
signature:
docs: docs/06-testing.md#testing-qa-pitfalls-1113
related: BUG-9178 BUG-9156 BUG-9107
switch: ui.bright_buttons
---

## Report
Owner (2026-11-13): "can we brighten the color of the buttons across the whole mod? The red and yellow buttons look a bit dim and grimy. I don't want them max brightness or max saturation, but would love more."

## Root cause
Observed: every button of the mod is a copy of the game's gold "Login" button of the account dialog (sui::button_like, D.like_button): a StdDrawable (vt 0x1B65238 on Steam) with four state images at +0x10 (images[0..3]: normal, disabled, highlighted, active: the hover showed images[2], runtime) and the layout at +0x30 (mode 2, cut 0.4 from each side, scale 1). Its gold measured (155..169, 133..146, 41..44) with a dark band (72, 62, 19) in the middle. The red buttons are that gold shaded by 0xFFAD535D (a multiply: brown, it can only darken).

## Fix
ui.bright_buttons (on, read when the Co-op window is built): data/ui/lineages_button_<gold|red>_<normal|disabled|hover|pressed>.png (tools/make_icons.py button: the game's pill shape, a dark rim, a light top edge, a vertical gradient; gold (246,214,98) / (224,184,58) / (184,146,34), red (226,92,74) / (198,60,46) / (150,38,30); hover x1.12, pressed x0.82, disabled greyed) drawn by a StdDrawable with the game button's layout (sui::std_drawable_states); the model button takes it, so every copy does; the red buttons (Report Issue, Delete, Leave, GIVE UP) take the red one (coop_ui apply_red) instead of the shade. Without the images (an older SporeCoop.package) the log says so and the game's look and the shade stay.

## Verification
no-fail-run: a look, manual. Before: out/shots/coopwin.png (TITAN). After: out/shots/bright-buttons-msi.png (the MSI: "ui: bright buttons: gold 27085934 red 270859C4"), out/shots/hover-states.png (Report Issue brighter under the mouse). Not seen: pressed and disabled on screen; TITAN (its installed package is older: decision 2W).

## History
- 2026-11-13: opened.
