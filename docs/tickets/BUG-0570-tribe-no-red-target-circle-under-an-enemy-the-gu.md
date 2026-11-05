---
id: BUG-0570
title: Tribe: no red target circle under an enemy the guest targets
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
The guest: it does not see the red circle under an enemy when it targets it.
Reports: SporeCoop-report-2026-11-04 191506 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* No log line. Inferred: the red target circle is drawn by the game for the player's own selected members'
  target; a guest's order goes to the host (cmds_sent) and its game sets no target of its own (P1).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab. Package P1.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), inferred.
