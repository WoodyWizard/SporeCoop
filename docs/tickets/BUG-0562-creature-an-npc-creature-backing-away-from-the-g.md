---
id: BUG-0562
title: Creature: an NPC creature backing away from the guest gets stuck inside its nest (video)
status: open
kind: bug
stage: Creature
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
The guest posted a video: a creature got stuck into the nest after backing away from it.
Reports: SporeCoop-report-2026-11-04 (Discord video) (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Discord video only, no log line. Inferred: the host's game walks the NPC back (its AI), the guest's proxy follows
  the host's path through the nest model; not analysed.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab. Package W2/P1 (not in the plan table).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open, not analysed (video only).
