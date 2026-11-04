---
id: BUG-0540
title: Space: a city's cultural targets stand at other places in each game (campaign_space)
status: open
kind: desync
stage: Space
reported: 2026-11-04
source: divergence detector (BUG-0484 split)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0484
---

## Report
Split from BUG-0484: the detector's `unmatched:cultural_target` (noun 0x3D5C325) in campaign_space only (3 / 3, 2
of 3 without a partner within 3 m, nightly 2026-11-03, out/lab/20261103-012624). Not seen in play_civ on
fix/w42-civ-left (out/lab/20261104-100146: no cultural_target row).

## Root cause
Observed (BUG-0484, fix/w41-civ-combat): the city setup 0xBE7D40 makes 4 cultural targets per city (city record
+0x618, +0x78 + 0x80 * i); their places are each game's own. Inferred: a worldobj kind (the host's places) would fit.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-civ-left): opened (split from BUG-0484).
