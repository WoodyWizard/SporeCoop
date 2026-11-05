---
id: BUG-0561
title: Creature: an allied nest has no ally (green) icon on the guest's minimap, only the grey discovered mark
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
The guest: an allied nest shows only the grey 'discovered' mark on its minimap, not the ally face.
Reports: SporeCoop-report-2026-11-04 (Discord text) (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* No log line (Discord text). Inferred: the minimap's ally face comes from the nest species' relation to the
  player's own species, set on the host only (P1, as BUG-0556).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab. Package P1.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), no evidence beyond the text.
