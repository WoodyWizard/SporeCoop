---
id: BUG-0572
title: Tribe: the guest's food counter is 0 while the host's tribe has food (from about an hour into the session)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-world-state
commits: bd97e36a
tests: tribe_food_zero (coop_tribe_food_zero.py), wsync_transitions (coop_wsync_transitions.py)
signature:
docs: docs/re/tribe.md#tribe-food-zero
related:
---

## Report
The guest: after about the 3rd restart it does not see their food in Tribe.
Reports: SporeCoop-report-2026-11-04 192403 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): The guest's food counter (bottom left) is 0 while the host's is 19, 22, 56 and 29 (s6519, s6939, s9174, s9694); earlier both showed the same (15/15 s5911, 78/78 s6037, 82/82 s6244, 5/5 s6403). So the food differs from about 19:17, before the restarts.
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (logs): the guest's food equals the host's (trace food=, every 10 s) until 19:15:48, when the guest's
  game spent its last 10 food ("shared food -10.0 from player 1" on the host); from 19:16 the guest shows 0 for
  good while the host has 15 .. 91. After the guest's rejoin the same: equal until its members ate the last food
  (-1, -1, -1, -0.6 at 19:56:12-17), then 0 for good. A drop to 0 that came from the host (19:53:45) did not stick.
* Observed (lab, tribe_food_zero): the game's own spending (cTribe::AddFood -n -> RemoveBundles) leaves the
  tribe's bundle vector empty (tribe+0x210 begin == end), and shared.cpp s_food only edits the amounts of
  existing bundles: `b == e` returns without a change, so no host total reaches the guest again.
  (docs/re/tribe.md F already notes AddFood "works on an empty container, unlike editing +0x134".)
  Not the restarts: it started after the second one by chance.

* Answer to "should the guest's game spend food at all": yes for what only the guest's game does (a purchase in its
  palette, BUG-0524), and those changes go to the host as the ledger's changes (shared.cpp MSG_RES_ADD); the host's
  total then comes back. The fix keeps that and makes the host's total reach a guest whose game has no bundle left.

## Fix
shared.cpp s_food: a positive change the bundles cannot hold (the container's vector empty, or never filled: both
pointers null, now read as 0 food) is made by the game's own `cTribe::AddFood` (it creates the bundles, as for an NPC
tribe's gathering); the fraction left goes into the new bundle as before. Switch `res.food_add` (off: the old code).

## Verification
Lab: tribe_food_zero (coop_tribe_food_zero.py) FAIL on 0.7.6 code: out/lab/20261104-201453 (host 30, guest 0 for 30 s). Package W1.

Lab: tribe_food_zero FAIL with `SPORE_FIX_OFF=res.food_add` out/lab/20261104-204248 (host 30, guest 0 for 30 s),
PASS out/lab/20261104-204124 (guest 30 within 1 s, still 30 20 s later). Class test wsync_transitions (the guest
spends all its food after a game over and a rejoin): FAIL out/lab/20261104-205648 (host 10, guest 0), PASS
out/lab/20261104-205651.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W1), reproduced in the lab (tribe_food_zero out/lab/20261104-201453 FAIL).
- 2026-11-04 (fix/w43-world-state, W1): fixed; see Fix and Verification.
- 2026-11-04 (fix/w43-detector2, D0 detector): `Tribe:state:food` once (the player tribe's food 109 on the host, 124 on the guest) in 293 Tribe comparisons, labs 20261104-203017 / 20261104-210208: the 0-vs-19..56 of the report was not reproduced in these sessions.
- 2026-11-05: released in 0.7.7.
