---
id: BUG-0511
title: Cell: after the cell grows (editor or level-up) the other cells float around very fast on the guest, and a visual glitch while growing
status: open
kind: desync
stage: Cell
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: guest
versions: 0.7.3.4, 0.7.3.6
fixed_in:
branch:
commits:
tests: 
signature:
docs:
related: BUG-0480, BUG-0041, BUG-0425
---

## Report
0.7.3.4 forum post: very big sync problems when the cell grows (a new cell stage level): cells float around very fast. A direct message (0.7.3.6): a visual bug when growing up (minor). No log line or screenshot names it; the 0.7.3.4 guest log shows the host's editor -> world reload pattern (BUG-0508) around the growth.
Target: 0.7.4 (Cell).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not analysed. Inferred: after a size change the guest's mirrors (driven to the host's positions) and the game's own scale of the view disagree for some seconds, or the guest's view zooms while mirrors keep the old scale's speed; may be the same as BUG-0480 (stale mirrors) after the pool swap at 'host back'. To reproduce: cell_coop with a level-up on the host (and one on the guest), watch the guest's mirrors' speed right after it.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage), report text only.
