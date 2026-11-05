---
id: BUG-0576
title: Tribe -> Civ: black screen after evolving from Tribe into the city stage
status: open
kind: bug
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: host
versions: 0.7.5
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0313
---

## Report
The host: black screen after evolving into the city stage from Tribe.
Reports: SporeCoop-report-2026-11-04 201550 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s10034: the host's screen is black except the co-op window.
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (host log): the evolve vote for Tribe ended at 20:14:59 ("evolution starts"), then "the editor waits:
  the game is busy (paused)" and for 50 s no stage change (still "tribe(host)" at 20:15:35) until the mark:
  the mod waits for the game to unpause while the game's Tribe -> Civ change waits (black screen).
* The 0.7.3.6 log carried in BUG-0578 ends the same way ("the editor waits: the game is busy (paused)", then the
  player quit).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab (campaign_* Tribe -> Civ not rerun). Package T1.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (T1), observed in the host log.
