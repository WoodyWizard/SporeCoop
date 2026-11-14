---
id: BUG-1097
title: lab: a warm-reset test inherits the faults an earlier fault test made on purpose
status: fixed
kind: tooling
stage: Creature
reported: 2026-11-14
source: lab 20261114-005006 same_view, npc_ai_lookup (final check of 0.7.14)
role: host
versions: rel/0.7.14
fixed_in: unreleased
branch: fix/w49-warmfault
commits: 3c921cc7
tests: kit_cmd_fault + same_view (--in-order), lab_selftest
signature: "lab: faults in the log: host faults: total faults: N" on a warm-reset test after kit_*_fault, 0xC0000005 at sporekit.dll+0x33ABB data=0x10
docs: docs/06-testing.md#testing-w49-warmfault
related: BUG-0966, BUG-1024, BUG-0975, BUG-0430, BUG-1099
---

## Report
Final check of 0.7.14, lab 20261114-005006: `same_view` (lane 5) failed "FAIL lab: faults in the log: host faults:
total faults: 5" and `npc_ai_lookup` (lane 4) "host faults: total faults: 1". Both started by a warm reset; both
passed cold in the rerun 20261114-022615. Their host game logs hold the faults the kit fault tests make on purpose
(`kit.test.cmdfault`, `kit.test.presentfault`, 0xC0000005 at sporekit.dll+0x33ABB data=0x00000010).

## Root cause
Observed. The lane order (results.json): lane 5 kit_cmd_fault (2 faults) -> kit_overlay_fault (3) -> same_view
(warm), lane 4 kit_present_fault (1) -> npc_ai_lookup (warm). The count `Checks.faults()` reads is SporeKit's
exception logger (`exc`, sporekit/commands.cpp c_exc): it lives in the game process and counts from the game's
start. `twoinst.warm_reset()` reset the session, holds, settings and the fixture but not this count, so the test
after a fault test reported the earlier test's faults as its own (5 = 2 + 3, the same x5 at sporekit.dll+0x33ABB).
The lab (tools/lab.py run_one) reads only the test's printed `host faults:` line, so the game log window was not the
cause. kit_*_fault are `reports=True` (they check their own faults), so they passed themselves; kit_overlay_fault
also printed the 2 faults of kit_cmd_fault before it (total 5 where its own are 3).

## Fix
`twoinst.clear_faults()`: a warm reset calls `exc clear` on both games first (the count starts at 0 as after a
cold start; a fault of the reset itself counts for the new test) and prints what it cleared ("(warm reset: the
faults of the tests before cleared: host 2, guest 0)"). The test before has read and judged them already: a
non-`reports` test fails on them, a `reports` test checks them itself. The kit fault tests stay warm.
`SPORE_WARM_KEEP_FAULTS=1` keeps the old way (the twin). lab_selftest checks both.

## Verification
- FAIL (the twin, `SPORE_WARM_KEEP_FAULTS=1`): out/lab/20261114-025716, kit_cmd_fault then same_view warm on lane
  6: same_view "FAIL lab: faults in the log: host faults: total faults: 2".
- PASS: out/lab/20261114-025857 (kit_cmd_fault, same_view warm: 2/2, "faults of the tests before cleared: host 2");
  out/lab/20261114-030034 (kit_cmd_fault, kit_overlay_fault, kit_present_fault, npc_ai_lookup, each warm after the
  first: 4/4; kit_overlay_fault now reports its own 3 faults).

## History
- 2026-11-14: opened.
- 2026-11-14: fixed on fix/w49-warmfault (3c921cc7).
