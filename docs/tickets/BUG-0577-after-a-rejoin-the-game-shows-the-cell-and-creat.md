---
id: BUG-0577
title: After a rejoin the game shows the Cell and Creature stages as not done yet (stage progress lost)
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
The guest: after rejoining it shows that they need to do the cell stage and the creature stage, but they already did that.
Reports: SporeCoop-report-2026-11-04 (Discord text) (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Discord text only, no log of the rejoin. Inferred: the guest's stage progress (the History bar of the stages
  done) is its own save's; nothing sends the host's (W1: stage progress).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab. Package W1.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W1), no evidence beyond the text.
