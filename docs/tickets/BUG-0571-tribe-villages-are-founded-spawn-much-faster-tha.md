---
id: BUG-0571
title: Tribe: villages are founded (spawn) much faster than in the single-player game
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
related: BUG-0483
---

## Report
The host: villages in Tribe spawn so much faster; on Discord: the villages spawn so fast they cannot get through the tribal stage.
Reports: SporeCoop-report-2026-11-04 191714 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (guest log): forms_blocked=61, npc_tribes_made=4 (the guest's own forming is blocked, BUG-0483; the
  4 tribes are the host's). The host's game formed them; its log has no forming line.
* Inferred: the host's three reloads (s 5850, 6391, 7320) and its gifts are not the cause shown; the NPC tribe
  controller's pace with two players' members in one tribe was not measured.
2026-11-04 (fix/w43-realign): not reproduced. Observed (lab tribe fixture): the host's world forms NPC tribes with its own controller (`coop.tribe form`: 2 -> 3, the planet then lists 5 villages; the save had 2); a guest forms none (tribe.guest_tribes). Inferred: every host game over reloads a save with fewer villages, and the controller forms them again ("fewer than its property list allows", docs/re/tribe.md#tribe-guest-world) at its own pace: three reloads in 25 min looked like fast spawning. Not measured: the controller's interval with two players' members in one tribe.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab (a 10-minute forming count alone vs in a session was not run). Package W2 (plan table).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W2), not reproduced.
- 2026-11-04 (fix/w43-realign): analysed, not reproduced, not fixed (no change to the host's controller).
