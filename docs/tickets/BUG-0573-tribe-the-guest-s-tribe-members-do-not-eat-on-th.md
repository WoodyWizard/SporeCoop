---
id: BUG-0573
title: Tribe: the guest's tribe members do not eat on their own, they must be ordered to eat
status: open
kind: bug
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
The guest: tribe members do not eat on their own, they have to be forced to eat.
Reports: SporeCoop-report-2026-11-04 200118 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (logs): the report (s 9174) came after the guest's food stuck at 0 (from 19:56:52, BUG-0572); the
  guest's game had run its own members' eating (the -1 food changes it sent at 19:56).
* Inferred: with no food bundles in the guest's game its members' hunger AI finds nothing to eat; an order goes to
  the host and is carried out there. A consequence of BUG-0572 (W1).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced on its own; the cause BUG-0572 is (tribe_food_zero out/lab/20261104-201453 FAIL). Package W1.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W1), a consequence of BUG-0572.
