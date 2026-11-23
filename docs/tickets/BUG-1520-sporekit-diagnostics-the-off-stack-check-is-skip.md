---
id: BUG-1520
title: SporeKit diagnostics: the off-stack check is skipped for a repeated fault site and after the 64-site cap (KIT-13)
status: fixed
kind: bug
stage: All
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in: unreleased
branch: fix/w80-net
commits: a0024f85
tests: regression: kit_crash, report_shot
signature:
docs:
related: 
switch: kit.offstackcap
---

## Report
External static audit of 0.7.15.5, item KIT-13 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-kit.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (sporekit/exceptions.cpp veh): the dedup (`x.count++; return`) and the cap (`g_recs.size() >=
64`) returned before the off-stack check (BUG-0956: a fault whose esp is outside its thread's stack is never dispatched by
Wine; the VEH is the only reporter). After 64 distinct fault places in one run (the game's caught faults, IsBadReadPtr
probes), or a second fault at a known place, such a fault got no crash report and no real stack. The other parts of the
audit row are theoretical or false (see out/research/ext-audit-verify-kit.md).

## Fix
exceptions.cpp `offstack_check()` (the BUG-0956 block, once per run); the dedup and cap paths call it before they
return. Switch `kit.fix offstackcap 0` (0.7.16.1).

## Verification
* no-fail-run: the failing case is a fault Wine cannot dispatch (the game ends at once) after 64 distinct fault places;
  no test seam makes 64 places and an off-stack fault on demand. The change only adds the existing check to the two early
  returns (no new behaviour for a normal fault). Regression: `SPORE_DETECTOR=20 python3 tools/lab.py run kit_crash --lane N --retry 0 --no-build`.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fixed on fix/w80-net (a0024f85); closed with no-fail-run.
