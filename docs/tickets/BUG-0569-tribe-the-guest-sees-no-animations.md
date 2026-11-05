---
id: BUG-0569
title: Tribe: the guest sees no animations
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
The guest (3 reports): it does not see any animations in Tribe.
Reports: SporeCoop-report-2026-11-04 191438 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s6375 / s6403: the host sees its members and chiefs fighting in the village; the guest's screen of the same village shows no members (s6403) or creatures standing still (s6375).
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
Observed (host log): the host's game went Tribe -> Loading -> Tribe at s 5850, 6391 and 7320 (19:06:05, 19:15:06, 19:30:35: a game over and the game's reload of the last save; at 19:06 its members 25 -> 3) while the guest's game stayed in its world (no stage change in its log, only its mirrors 25 -> 3). Observed (lab): tribe_bounce_world out/lab/20261104-202045 FAIL: after the host's game over and reload the guest's game keeps the 3 NPC tribes the host's reloaded world does not have (emptied to 0 members by BUG-0483, but the tribe, its hut and map mark stay). Nothing re-aligns a guest's planet after the host's reload (W2).
* s 6375 / 6403 are after the second reload (s 6391): the guest's village lacks the members the host fights with.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Same mechanism as BUG-0564 (tribe_bounce_world out/lab/20261104-202045 FAIL). Package W2.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W2), the class reproduced by tribe_bounce_world.
