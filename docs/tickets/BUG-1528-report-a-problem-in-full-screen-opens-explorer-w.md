---
id: BUG-1528
title: Report a problem in full screen opens Explorer, which minimizes Spore (the report's own log then shows a 20-30 s stop)
status: open
kind: bug
stage: UI
reported: 2026-11-23
source: discord
role: 
versions: 0.7.16.1 and earlier
fixed_in: 
branch: fix/w80-net
commits: a81c9717
tests: tools/tests/coop_report_fullscreen.py (report_fullscreen, twin report_fullscreen_off)
signature:
docs:
related: BUG-0779
switch: core.report_no_explorer
---

## Report
Player report on Discord ("Screen tearing", 2026-11-16; out/research/dc-report-tearing-20261116.md, "Tickets" 1): in exclusive full screen the report's Explorer window takes the foreground, the game pauses itself while minimized. Fix idea: do not open Explorer in exclusive full screen (the in-game panel names the file), or only from a button, or after the game is left.

## Root cause
Observed in the player's second report archive (out/research/dc-report-tearing-20261116.md, zip 1 16:33:22 and the
report run of zip 2): `ui: click report`, the archive written, then frames stop for 27 s and come back with a second short
hitch. Code (coop_ui.cpp finish_report): `ShellExecuteW(... "explorer.exe", "/select,<zip>")`; in exclusive full screen
Explorer takes the foreground and the game minimizes itself (the game's own focus pause), so the report's own log shows a
"freeze" and the player has to Alt+Tab back.

## Fix
coop_ui.cpp: while the game window fills its monitor (no caption, the monitor's whole rectangle: exclusive or
borderless full screen) the folder is not opened; the status says "Report saved in SporebinEP1\mods\logs: <file> (the
folder is not opened while the game is in full screen)"; the log says which way it went. Windowed play opens Explorer as
before. Test seam `coop.uistate fullscreen 1|0|auto`. Switch `core.report_no_explorer` (off: 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: report_fullscreen, report_fullscreen_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run report_fullscreen --lane N --retry 0 --no-build` (full screen: no Explorer, the status names the folder;
  windowed: Explorer as before).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run report_fullscreen_off --lane N --retry 0 --no-build`.
* Not verifiable in the lab: the real full-screen detection (the lab plays windowed); the rule is GetWindowRect against
  GetMonitorInfo and WS_CAPTION.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-N1), branch fix/w80-net.
- 2026-11-23: fix and tests on fix/w80-net (a81c9717); waiting for the night lab run.
