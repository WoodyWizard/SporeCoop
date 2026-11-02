---
id: BUG-0380
title: civ: vehicles missing on guests and money totals differ in campaigns (sync-check v missing, p apart, totals)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-01
source: lab log gates report over recorded runs (feat/w30-lab-gates, 2026-11-01)
role: guest
versions: 0.7.3.1 - main 15539ad
fixed_in: 0.7.3.4
branch: fix/w31-civ-gate
commits: fe88228 e052b04 cea77bf e2aba88
tests: civ_city_burst (coop_civ_city_burst.py), civ_cursor (coop_civ_cursor.py), gates_selftest.py (monitor totals)
signature: sync-check: MISMATCH ... (Civilization ...): v: N missing on the guest / p: 1 apart (pN ...); MONITOR-VIOLATION totals ... money
docs: docs/re/civ.md#civ-city-burst, docs/06-testing.md#testing-civ-gates
related: BUG-0120, BUG-0319, BUG-0325, BUG-0122, BUG-0186, BUG-0381
switch: civ.city_burst diag.rts_cursor diag.rejoin_grace
---

## Report
`lab.py gates`: Civ v missing in 35 results, Civ p apart in 29 (campaign_space 45/45 red). Monitor: Civ money host 10960 vs both guests 10590 for 20+ s in campaign_space (42 violations). Related: BUG-0319, BUG-0325.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Three separate things behind the three symptoms:

1. **v missing** (observed, logs + runtime). The campaign founds the cities still to come with
   `SpawnNextCity(ex, 1, 0)` (instant), which the co-op hook does not broadcast (only `!instant &&
   !who`); guests got them only from BUG-0120's catch-up, one city per 15 s (6 s short + 15 s
   since the last founding): 6 cities took 75 s on a campaign guest (lab 20261031-205600
   `founding the next site` every 15 s). A city that does not exist on the guest has no
   civilization there, so `guest_state` cannot create the mirrors of its nation's vehicles
   (`CivByPol` -> null): `v: 6 missing on the guest (v13..v18)` on all three guests. civ_city_join
   (3/12 red) is the same catch-up after a founding lost during the join. The real game's own
   tick founds with (0, 0) and is broadcast, so in normal play only a join hits it.
2. **p apart** (observed, test). In Tribe / Civ a player is the planet point under the mouse
   (also moved by the camera). The partner's copy trails ~0.1 s + half a round trip: 45 m median
   while the mouse moves (coop_civ_cursor.py, coop.sync p0 on both games), 0.0 m at rest. The
   sync-check's 10 m player limit therefore logged `p apart` (548 lines in campaigns, median 23.5 m,
   max 363 m: capture cinematics move the camera under a still mouse). Not a desync.
3. **money totals** (observed in the recorded logs, inferred as sampling). Money is the host's
   total, broadcast on every change; both guests always had the same value (they get the same
   message). The monitor compared a guest's trace line with the host's lines within +-6 s; trace
   lines come every 10 s, so often one host line was in the window, and a treasury growing ~400
   per 10 s put the guest's value between two host lines (e.g. guest 10590 at s 438, host 10590 at
   s 430 and 10960 at s 440). Every one of the 42 campaign_space violations has the guest's value
   between the host's lines before and after it; none was a lasting difference.

## Fix
* `civ.city_burst` (fe88228): the catch-up reads the city simulator's own founding count
  (cCivExpansion +0xAC sites, +0xB0 sites left: founded = +0xAC - +0xB0, lowered at the call, the
  city appears ~4 s later) and founds `host cities - founded` at once (after 2 s short); it never
  founds while its foundings are only on their way (a paused game). Off: one per 15 s.
* `diag.rts_cursor` (e052b04): the sync-check leaves out a Tribe / Civ pointer that moved within
  the last second on either machine (players_out); a resting pointer is compared. Tribe's `p apart`
  (BUG-0381) likely has the same cause: not verified in Tribe.
* `diag.rejoin_grace` (e2aba88): the host kept the sync-check entry of a player who left, so the
  same id joining again was checked on its old schedule; in civ_city_join (lab 20261101-100204,
  test OK, gate red) 6 s after the guest's world load, while the city the catch-up had just founded
  was still on its way (`v: 1 missing`). Entries of players who left are dropped: a returning
  player is first checked 10 s after the join, like a newcomer.
* Monitor (cea77bf, tools only): `totals` always includes the host's lines right before and after
  the guest's line. A replay over the recorded runs: campaign_space totals 42 -> 0.

## Verification
* civ_city_burst: FAIL with `SPORE_FIX_OFF=civ.city_burst` lab 20261101-094048 (after 31 s the
  guest still lacked city 6 and vehicle 7; log gate MISMATCH v missing); PASS lab 20261101-094247
  (2 runs, all 3 cities and vehicles in 7 s, no city twice).
* civ_cursor: FAIL with `SPORE_FIX_OFF=diag.rts_cursor` lab 20261101-094724 (MISMATCH p0 74.9);
  PASS lab 20261101-094852 (2 runs, no mismatch, resting pointer 0.0 m apart).
* civ_city_join (rejoin grace): FAIL with `SPORE_FIX_OFF=diag.rejoin_grace` lab 20261101-104227
  (gate: MISMATCH v: 1 missing, the test itself OK); before the switch existed the same in lab
  20261101-100204; PASS lab 20261101-103417 (3 runs, gates on).
* campaign_space (gates and monitor on): lab 20261101-105523 (city burst, the old instant=1
  shortcut): Civ 1 MISMATCH (v19, a city on its way) instead of 6-8 per guest, no monitor
  violation; lab 20261101-110949 (shortcut now (ex, 0, 0), broadcast): no Civ MISMATCH, no monitor
  violation (totals, stage_apart); still red on Space `p apart` (p0 14.7 / 24.3, BUG-0378's area).
* Monitor: gates_selftest.py "a growing treasury sampled between two host lines" fails on the old
  monitor, passes now; "a treasury 10000 apart" stays a violation.

## History
- 2026-11-01: opened.
- 2026-11-01 (fix/w31-civ-gate): three causes found and fixed (above); the campaign's Civ shortcut founds with (ex, 0, 0) now (broadcast, as the game's tick).
- 2026-11-02: released in 0.7.3.4.
