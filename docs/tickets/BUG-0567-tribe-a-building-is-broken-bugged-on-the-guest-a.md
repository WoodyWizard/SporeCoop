---
id: BUG-0567
title: Tribe: a building is broken (bugged) on the guest after the guest dies
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-realign
commits: 736eaef5
tests: realign_tribe (coop_realign_planet.py --fixture tribe --case gameover)
signature:
docs: docs/re/tribe.md#tribe-realign
related:
---

## Report
The guest: a bugged building after dying (2 reports, screenshots in the report).
Reports: SporeCoop-report-2026-11-04 190900 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s6037 / s6054: after the guest's death its screen is dark and covered with grey smoke puffs over the village, a forge smoking; the host's screen of the same village is clear.
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
Observed (host log): the host's game went Tribe -> Loading -> Tribe at s 5850, 6391 and 7320 (19:06:05, 19:15:06, 19:30:35: a game over and the game's reload of the last save; at 19:06 its members 25 -> 3) while the guest's game stayed in its world (no stage change in its log, only its mirrors 25 -> 3). Observed (lab): tribe_bounce_world out/lab/20261104-202045 FAIL: after the host's game over and reload the guest's game keeps the 3 NPC tribes the host's reloaded world does not have (emptied to 0 members by BUG-0483, but the tribe, its hut and map mark stay). Nothing re-aligns a guest's planet after the host's reload (W2).
* s 6037/6054: smoke over the village on the guest only, after the reload of s 5850 (inferred: a building of the old world).
2026-11-04 (fix/w43-realign): observed in the code: nothing synced a hut's or tool's health (cCombatant at +0x120, health +0x158, maximum +0x14C); a guest's game keeps the damage of its own world after the host's reload (the host's huts are whole again). Inferred: the smoke and the broken building on the guest's screen were its own world's damaged building (the report's screenshot shows the guest's smoking building at the host's clear village). Not seen: whether the smoke effect follows the health field.

## Fix
The whole-planet list (realign.cpp) carries each hut's and tool's health in 1/1000; a guest's bound hut / tool more than 2 % off gets the host's (never 0: the guest's game runs no defeat). `coop.fix realign.buildings off`: each game keeps its own (0.7.6).

## Verification
realign_tribe: every NPC hut but one damaged to 30 % in the guest's game only, then the host's game over: FAIL with `SPORE_FIX_OFF=realign.villages,realign.buildings` out/lab/20261104-212017, PASS out/lab/20261104-212353 (every hut at the host's health). Not verified: the smoke / model on screen (no screenshot comparison).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W2), the class reproduced by tribe_bounce_world.
- 2026-11-04 (fix/w43-realign): fixed (realign.buildings); runs above.
- 2026-11-05: released in 0.7.7.
