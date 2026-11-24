---
id: BUG-1518
title: Co-op window: the give button and the jump arrow run over the creation label with long names (UIFX-8)
status: fixed
kind: bug
stage: UI
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w80-net
commits: d23e5cf0
tests: tools/tests/coop_ui_give_long_name.py (ui_give_long_name, twin ui_give_long_name_off)
signature:
docs:
related: BUG-1062
switch: core.ui_give_clamp
---

## Report
External static audit of 0.7.15.5, item UIFX-8 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-uifx-cell.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (coop_ui.cpp the give row, BUG-1062's fit): the give button grew right to left as wide as its
caption with no lower bound, and the jump button sat left of it: a partner name of 24 wide letters (the name limit) made
"Give to <name>" start left of the end of the creation label ("selected tribe members: N"), a long star name pushed the jump
button toward the window's edge. Typical names are not affected.

## Fix
coop_ui.cpp: the give button starts at x 400 at the most (kGiveMinX1), the jump button at 240 (kJumpMinX1); a caption
too wide for that keeps its prefix and the name is cut by whole UTF-8 code points with "..." (`cut_caption`). Switch
`core.ui_give_clamp` (off: 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: ui_give_long_name, ui_give_long_name_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run ui_give_long_name --lane N --retry 0 --no-build` (a guest named 24 x W, the host selects a member: the give
  button fits its cut caption and overlaps no label; key frame).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run ui_give_long_name_off --lane N --retry 0 --no-build` (the button over the label).
* Regressions near it: space_give_pick, ui_golden.
0.7.17 release lab: PASS: ui_give_long_name (lab 20261124-020506); no-fail-run: verified by 0.7.17 release gate lab run.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fix and tests on fix/w80-net (d23e5cf0); waiting for the night lab run.
- 2026-11-24: released in 0.7.17.
