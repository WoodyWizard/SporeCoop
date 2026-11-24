---
id: BUG-1529
title: Diagnostics: a stop while the game window is minimized or not in the foreground is logged as a hitch and counted in min fps and the longest frame
status: fixed
kind: bug
stage: All
reported: 2026-11-23
source: discord
role: 
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w80-net
commits: 68fe88fe, eb6cb148
tests: tools/tests/coop_perf_pause.py (perf_pause, twin perf_pause_off)
signature:
docs:
related: BUG-0779, BUG-0687
switch: diag.hitch_pause
---

## Report
Player report on Discord ("Screen tearing", 2026-11-16; out/research/dc-report-tearing-20261116.md, "Tickets" 2): the long stops were the game's own unfocused pause, but the perf lines and the keepalive line read like freezes. Fix idea: tag the hitch and the BUG-0687 keepalive lines with IsIconic / foreground, keep such gaps out of min fps / longest frame or name them.

## Root cause
Observed in the player's second report archive (out/research/dc-report-tearing-20261116.md): every long stop was the
game's own pause with the window minimized / not in the foreground (Canvas::Tick's pump, `busy=paused` after it; also without
a session), and each was logged as `perf: hitch N ms` and counted in the minute's min fps and longest frame like a freeze,
so the report read as "freezes of 28 s" in multiplayer. The keepalive's BUG-0687 line did not say the window's state.

## Fix
diag_stats.cpp: a poll thread (every 100 ms, ended before a hot reload unloads coop.dll) notes whether the game window is
minimized or not in the foreground; a frame gap in which it was is logged as `perf: pause N ms (window minimized /
unfocused: the game's own pause, not a freeze; ...)`, kept out of min fps and the longest frame, counted per minute (`perf:
pauses N this minute ...`, a line of its own: tools/loggates.py parses the minute's perf line); the keepalive line names
`(window minimized M, foreground F)`. In the lab's background mode the game counts as in front (hitches stay hitches). Test
seam `coop.diag pausemark 1|0`. Switch `diag.hitch_pause` (off: 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: perf_pause, perf_pause_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run perf_pause --lane N --retry 0 --no-build` (a 4 s stop while marked unfocused is a pause, the keepalive says
  foreground 0, the longest frame leaves it out; unmarked it is a hitch).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run perf_pause_off --lane N --retry 0 --no-build`.
0.7.17 release lab: PASS: perf_pause (lab 20261124-020506); no-fail-run: verified by 0.7.17 release gate lab run.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-N1), branch fix/w80-net.
- 2026-11-23: fix and tests on fix/w80-net (68fe88fe, eb6cb148); waiting for the night lab run.
- 2026-11-24: released in 0.7.17.
