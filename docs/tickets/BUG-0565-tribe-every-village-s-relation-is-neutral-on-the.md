---
id: BUG-0565
title: Tribe: every village's relation is neutral on the guest, while the host sees them aggressive and slightly aggressive
status: open
kind: desync
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
The guest: all villages are neutral to it; for the host they are aggressive and slightly aggressive.
Reports: SporeCoop-report-2026-11-04 190729 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (code): nothing in the mod sends or compares tribe relations (cRelationshipManager values by
  political id; docs/re/tribe.md B): each game keeps its own; the host's raids and gifts change the host's only.
* Inferred: the guest's game never gets the host's raids' relation events, so its villages stay neutral (W1).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab (the `call` console command returns eax only, CalculateRelationship returns a float in ST0: a relation read needs a command). Package W1.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W1), observed in code, not reproduced at runtime.
