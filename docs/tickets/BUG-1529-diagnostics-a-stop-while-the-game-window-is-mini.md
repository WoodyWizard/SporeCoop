---
id: BUG-1529
title: Diagnostics: a stop while the game window is minimized or not in the foreground is logged as a hitch and counted in min fps and the longest frame
status: open
kind: bug
stage: All
reported: 2026-11-23
source: discord
role: 
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w80-net
commits:
tests:
signature:
docs:
related: BUG-0779, BUG-0687
---

## Report
Player report on Discord ("Screen tearing", 2026-11-16; out/research/dc-report-tearing-20261116.md, "Tickets" 2): the long stops were the game's own unfocused pause, but the perf lines and the keepalive line read like freezes. Fix idea: tag the hitch and the BUG-0687 keepalive lines with IsIconic / foreground, keep such gaps out of min fps / longest frame or name them.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-N1), branch fix/w80-net.
