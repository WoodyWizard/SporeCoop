---
id: BUG-1519
title: Support summary: the peer names of an earlier session stay in the next hosted session (UIFX-10)
status: fixed
kind: bug
stage: All
reported: 2026-11-23
source: audit
role: host
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w80-net
commits: 9e11a529
tests: tools/tests/coop_diag_second_session.py (diag_second_session, twin diag_second_session_off)
signature:
docs:
related: 
switch: diag.peer_names_reset
---

## Report
External static audit of 0.7.15.5, item UIFX-10 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-uifx-cell.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (diag_stats.cpp): `g_peer_names` (a name seen again counts as a rejoin) was cleared only in
`on_connected` (a guest's connect); `reset_counters` cleared `g_c.peers` but not the names. A host's second session in the
same game run found the returning guest's name, counted `rejoins++` and never listed it: "players none; rejoins 1".

## Fix
diag_stats.cpp `on_session_start` clears the names too. Switch `diag.peer_names_reset` (off: 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: diag_second_session, diag_second_session_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run diag_second_session --lane N --retry 0 --no-build` (the host ends its session, hosts again, Bob joins: the
  summary names Bob, rejoins 0).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run diag_second_session_off --lane N --retry 0 --no-build`.
0.7.17 release lab: PASS: diag_second_session (lab 20261124-020506); no-fail-run: verified by 0.7.17 release gate lab run.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fix and tests on fix/w80-net (9e11a529); waiting for the night lab run.
- 2026-11-24: released in 0.7.17.
