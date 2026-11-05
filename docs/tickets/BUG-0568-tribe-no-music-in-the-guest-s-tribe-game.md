---
id: BUG-0568
title: Tribe: no music in the guest's Tribe game
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
The guest: it has no music in the tribe stage.
Reports: SporeCoop-report-2026-11-04 191227 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* No log line about music. Inferred: the game's Tribe music follows the player tribe's own state (fights, the
  village's mood); the guest's tribe holds only mirrors. Not analysed.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced (the lab games run muted). Not in the plan table: P1 (the guest as a player) suggested.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open, not analysed (lab games are muted).
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): not analysed in P1 (lab games are muted).
