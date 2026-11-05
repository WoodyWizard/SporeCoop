---
id: BUG-0430
title: class: tests and harness that hide failures (umbrella)
status: open
kind: bug
stage: Tests
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Not covered: warm tests switch fixes off without the lane record (coop_civ_ruin_repair.py:33, coop_alt_enter.py:95, coop_guest_missing_pack.py:97); knobs reset only on success (coop.worldfix retry 0); BUG-0356 probably this; bookkeeping asserts (coop_party_creature:84/123, coop_creature_kills:125, coop_cell_away:122; tests of 0010, 0011, 0040, 0059, 0064, 0066, 0082); tests that cannot fail (0095, 0096, 0098, 0172, sync_lowfps, 0340 scenario); coop_nest_objects.py in no suite; 0109/0110/0122/0169 unit only; fresh-only / tolerance filters (0321, 0379) hide lateness; BUG-0021 status stale; 0353 == 0362; re_lint does not scan tools/*.py; no .sym return-type check (ApplyRelationship lacks the ST0 note).
General fix: warm_reset restores all switches and knobs; lint asserts on coop.* counters; make test checks suite membership and that every fixed ticket names a switch or reason; `returns:` in .sym checked by re_lint.
Source: docs/generalisation-plan.md G14.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
- 2026-11-04 (fix/w43-tests): G1 (fix/w43-tests): tribe_guest_tribes' twin run passed step 1 on the bug since W2 (BUG-0587); play_tribe calibrated during the cold join pause (BUG-0586); cell_coop's meal (BUG-0356); cell_new_game's empty screen (BUG-0293); the proxies gate was right (BUG-0503, a real desync fixed). Section docs/06-testing.md#testing-harness-g1.
- 2026-11-05 (fix/w44-tests, G2): long_creature_short (rel/0.7.8 out/lab/20261105-123757, lane 4) failed "restart:
  console error TimeoutError('timed out')" and so "the games survived the session": the restarted guest joined at
  12:45:17, loaded the shared world until 12:46:09 (52 s) and played on (its game log); the console calls of
  Session.restart / twoinst.wait_guest_in timed out while it loaded and the first exception ended the transition. Kind:
  harness (it measured the console's answer time, not the game). coop_long_session.py: restart, rejoin, menu and
  wait_together take a console timeout as "not yet" (busy_ok) and wait on until their own deadline (240 s join, 120 s
  together, as wait_together already had); a guest that never comes back still fails. Not rerun in the failing
  timing (a 52 s load is not reproducible on demand). G2's other cases: docs/06-testing.md#testing-harness-g2
  (BUG-0397, BUG-0437, BUG-0601, BUG-0589, BUG-0587, BUG-0440).
