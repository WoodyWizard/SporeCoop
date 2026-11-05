---
id: BUG-0556
title: Creature: creatures the host summons with Summon Flock are not friendly (green) to the guest
status: open
kind: bug
stage: Creature
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: both
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
Both players: the flock the host calls is not allied with the guest; the guest does not see them as friendly.
Reports: SporeCoop-report-2026-11-04 180352, 180419 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s2129 / s2144: the host's screen shows its flock with hearts (allied); at s2144 the guest's world is black (only the HUD) while the host gets "<guest> is responding again" (a guest hitch).
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (logs, host and guest at s 2129 / 2144): no mod line for the host's Summon Flock; the flock is the
  host's game's own allied herd. Inferred: the guest's proxies of those NPCs get the relation of their species
  (BUG-0462, coop.social rel) but not the host's "summoned, in my pack" flag, so they are not allied on the
  guest (P1: the host's flock as the guest's allies).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab (no test calls Summon Flock). Package P1.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), not reproduced.
