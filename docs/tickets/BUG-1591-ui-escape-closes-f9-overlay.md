---
id: BUG-1591
title: UI: pressing Escape while F9 co-op window is open opens Spore Pause Menu behind it instead of closing F9
status: fixed
kind: bug
stage: UI
reported: 2026-11-26
source: AI playtest (v0.8.2.3)
role: both
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 20fa22f2
tests: make test
signature: F9 overlay stays open when Escape is pressed and Pause Menu opens behind it
switch: core.ui_esc_close
docs:
related:
---

## Report
During 4-player AI playtests, pressing `Escape` while the F9 co-op overlay was visible opened Spore's native Pause Menu underneath instead of dismissing F9.

## Root cause
`mouse.cpp` did not intercept `VK_ESCAPE` on `WM_KEYDOWN` when the F9 window was open without a native game modal on top.

## Fix
Added `COOP_FIX("core.ui_esc_close", "BUG-1591")` in `mods/coop/coop_ui.cpp` and a key hook in `mods/coop/mouse.cpp` / `mods/coop/mouse.h` that swallows `VK_ESCAPE` while F9 is open (with no native game modal above it) and closes the F9 window cleanly.

## Verification
- PASS run: `out/ai-playtest/20261127-075046` (6.5-hour 4-player Cell -> Creature run, 0 crashes/faults) and `make test`.
- no-fail-run: UI input interception verified live in Wine and in `make test` (`core.ui_esc_close`).
