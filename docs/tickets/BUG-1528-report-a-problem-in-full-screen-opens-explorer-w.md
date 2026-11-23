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
commits:
tests:
signature:
docs:
related: BUG-0779
---

## Report
Player report on Discord ("Screen tearing", 2026-11-16; out/research/dc-report-tearing-20261116.md, "Tickets" 1): in exclusive full screen the report's Explorer window takes the foreground, the game pauses itself while minimized. Fix idea: do not open Explorer in exclusive full screen (the in-game panel names the file), or only from a button, or after the game is left.

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
