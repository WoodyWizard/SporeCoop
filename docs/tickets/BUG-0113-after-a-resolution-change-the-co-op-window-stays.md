---
id: BUG-0113
title: after a resolution change the co-op window stays at its old position, partly off-screen
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#92
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-sys
commits: 5f0c909
tests: tools/tests/coop_window_layout.py recenter (suite window_layout)
signature:
docs: docs/06-testing.md#testing-sys-w3
related: #92
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/92 (full text, discussion).

The co-op window (760 x 600) is centred once, when it is built the first time F9 or the Co-op item opens it. The window is then kept for the whole run (`add_ref`), and nothing moves it again. If the player later picks a smaller resolution in the game's options, the window keeps the coordinates computed for the old screen size. Its bottom and right parts can end up outside the screen.

### Where
- The only place the window is positioned: [coop_ui.cpp#L395-L398](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L395-L398), in `build_dialog`, which runs only while `D.root` is null ([coop_ui.cpp#L708-L709](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L708-L709)).
- The window survives mode changes on purpose: [coop_ui.cpp#L373](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L373).
- SporeKit already fires `SK_EV_DEVICE_RESET` and updates `screen_size` on every Reset: [d3d9hook.cpp#L314-L330](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/d3d9hook.cpp#L314-L330). coop_ui does not listen to it.

### How to reproduce
1. Start at 1920 x 1080 and open F9 once (close it again).
2. In the game's options, set the resolution to 1280 x 720.
3. Join or host and press F9: `coop.uistate` shows `leave` / `ready` / `report` with y beyond 720, and the buttons are not visible.

### Expected
The window is centred on the current screen every time it opens (or after every device reset).

## Root cause
**What happens**

Example: the window is opened at 1920 x 1080, so its area is x 580..1340, y 240..840. The player switches to 1280 x 720 in the options. The window stays at 580..1340 / 240..840:
- The lobby's Ready / Send my world / Leave row (window y 488..528) lands at screen y 728..768, below the screen.
- The Report a problem button and the version footer are also off-screen.
- The right 60 px of the window are cut.

The other direction (small to large) leaves the window off-centre.

Alt+Enter is not a trigger: it calls SetVideoMode with -1 for the size and keeps the resolution ([docs/re/graphics.md](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/graphics.md#L20-L28)).

### Confidence
PLAUSIBLE, low severity. Our code never repositions the window; a second reviewer verified that. Not verified at runtime: whether the game's own layout re-anchors the account-dialog layout (0x34A6339E) after a resolution change.

## Fix
Fixed on fix/w3-sys (5f0c909). `place_window` centres the window on the current screen size when it
is built and every time it is opened (only when the size changed since). Runtime finding: the game's
UI does not re-anchor the window after `Renderer_SetVideoMode` (the old position stays).
`coop.corefix window_recenter 0` = centred once.

## Verification
Test: `tools/tests/coop_window_layout.py` (suite `window_layout`): open at 1440x1080, set 1280x720
through `SporeCoop::Renderer_SetVideoMode`, open again, every visible widget inside the screen.
* FAIL with `--old` (manual runs on lane 3, 2026-10-28): "Report a problem" at y 782..814 (screen 720).
* PASS with the fix: 2 manual runs (the window at 260..1020 / 60..660) and lab run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #92.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-sys (5f0c909); tests named above, lab run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).
- 2026-10-28: released in 0.7.3.0.
