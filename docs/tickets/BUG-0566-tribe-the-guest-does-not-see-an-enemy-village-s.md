---
id: BUG-0566
title: Tribe: the guest does not see an enemy village's hut (hub) health go down when the players attack it
status: fixed
kind: desync
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
The guest: it does not see the enemy village's hub hit points going down.
Reports: SporeCoop-report-2026-11-04 190729 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s5771: the host's screen has the village's hut with its health bar (6/6, 375) and the fight; the guest's screen at that spot has no hut and no fight.
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
Observed (host log): the host's game went Tribe -> Loading -> Tribe at s 5850, 6391 and 7320 (19:06:05, 19:15:06, 19:30:35: a game over and the game's reload of the last save; at 19:06 its members 25 -> 3) while the guest's game stayed in its world (no stage change in its log, only its mirrors 25 -> 3). Observed (lab): tribe_bounce_world out/lab/20261104-202045 FAIL: after the host's game over and reload the guest's game keeps the 3 NPC tribes the host's reloaded world does not have (emptied to 0 members by BUG-0483, but the tribe, its hut and map mark stay). Nothing re-aligns a guest's planet after the host's reload (W2).
* At s 5771 the host fought at a hut the guest's game did not have at that spot: same divergence (inferred).
2026-11-04 (fix/w43-realign): observed in the code: a hut's health was never sent: the host's members' blows on an enemy hut lowered the host's hut only (the guest's mirrors run no fight). The missing hut at the host's fight (s 5771) is the reload's divergence (BUG-0564's villages: the host's reloaded world has its village elsewhere, inferred).

## Fix
realign.buildings (realign.cpp): the guest's hut / tool has the host's health within 2 s (the whole-planet list every 2 s). The village where the host fights is the host's after the reload (realign.villages, tribe.npc_tribes makes the ones the guest lacks).

## Verification
realign_tribe step 2: the host's NPC hut at 40 %: the guest's hut at 400/1000 within 10 s: FAIL with `SPORE_FIX_OFF=realign.villages,realign.buildings` out/lab/20261104-212017 (the guest's [1000]), PASS out/lab/20261104-212353 ([400]); the host's hut regenerates (400 -> 638 in 30 s) and the guest's follows (guest log). Not verified: the health bar drawn on the guest's screen (it reads the same field, inferred).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W2): the world divergence after the host's reload reproduced (tribe_bounce_world).
- 2026-11-04 (fix/w43-realign): fixed (realign.buildings); runs above.
- 2026-11-04 (fix/w43-detector2, D0 detector): `Tribe:planet:tribe_hut` in 7 % of the Tribe comparisons (huts on the guest's planet only), labs 20261104-203017 / 20261104-210208.
- 2026-11-05: released in 0.7.7.
