---
id: BUG-1541
title: UI: hide the co-op lobby window while taking Report a problem and /bug screenshots
status: fixed
kind: feature
stage: All
reported: 2026-11-24
source: owner request 2026-11-24 (after reviewing Discord bug report screenshots obscured by the lobby dialog)
role: both
versions: 0.7.17.1 and earlier
fixed_in: 0.7.99
branch: rel/0.7.99
commits: 87bc2eaf
tests: report_shot, report_shot_hide_off
signature:
docs:
related: BUG-0442, BUG-1528
switch: ui.report_shot_hide
---

## Report
When a player submits a `/bug` mark from the co-op lobby chat or clicks "Report a problem" in the co-op window, the
captured JPEG screenshot (`shots/mark-*.jpg` or `shots/report-*.jpg`) includes the large co-op lobby window right in
the middle of the screen, hiding ~40% of the game world and HUD.

## Root cause
`support::report_shot()` called `SkHost::report_shot()` immediately while `D.root` (the co-op dialog) was still visible
in UTFWin, so the next `Present` captured the backbuffer with the lobby dialog drawn on top of the game scene.

## Fix
When `ui.report_shot_hide` is on and the co-op window is open and visible, `coop_ui::defer_report_shot()` hides `D.root`
(`g_shot_hide = true`), waits until `refresh()` has applied the hide outside the frame plus one `Present` so a full game
frame renders without the lobby, then calls `support::report_shot_now()` right before SporeKit's
`report_shot_on_present()` captures that clean backbuffer, and restores `D.root` on the following frame.

## Verification
PASS: out/lab/20261124-163100 (report_shot, 0 faults: lobby crop diff to clean=8.8 vs to lobby=54.1, lobby restored immediately).
FAIL: out/lab/20261124-163106 (report_shot_hide_off: diff to clean=53.2 vs to lobby=3.3).
- 2026-11-24: released in 0.7.99.
