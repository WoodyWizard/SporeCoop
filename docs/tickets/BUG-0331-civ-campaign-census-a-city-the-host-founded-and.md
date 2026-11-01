---
id: BUG-0331
title: Civ campaign census: a city the host founded (and its 2 vehicles) missing on a guest that had its pause menu open
status: fixed
kind: test
stage: Civ
reported: 2026-10-30
source: lab 20261030-210914 campaign#3 (lane 7) civ/play 22:09:43
role: guest
versions: main be725db
fixed_in: 0.7.3.2
branch: fix/w17-tribe
commits: 2d79697
tests: civ_pause_city (coop_civ_pause_city.py)
signature:
docs:
related: BUG-0319, BUG-0289, BUG-0325
---

## Report
Lab 20261030-210914 campaign#3 (lane 7, run dir out/lane7/campaign/20261030-210927): [census]
civ/play 22:09:43 "Bob: v15 / v16: the host has it, the guest not (host 000021D0 alive=1 ...)", two
checks in a row (22:09:42, 22:09:43), while the same check said "sync: not compared (pause menu
open): Bob" both times.

## Root cause
Observed (Bob's game log): the host's two foundings arrived and were replayed ("here founded",
23:08:18 and 23:09:21 game clock); the second city had not appeared 15 s later ("the host has 6
cities, this game 5: founding the next site" 23:09:36; sync-check "v: 2 missing on the guest (v15
v16)" 23:09:38). v15 / v16 belong to nation 000021D0, the new city's. Bob's pause menu was open at
the census (the campaign's own note).
Observed (`coop_civ_pause_city.py`, lab 20261030-232554-fix-w17-tribe): a city founded while the guest's
pause menu is open is missing there (host 5 cities 5 vehicles, guest 4 / 4 after 12 s); 3 s after
Resume the guest has the host's cities and vehicles. The founded city appears through a timed
callback of the game, and the guest's pause stops its game time.
So the census compared a paused guest (the sync check skips those since BUG-0289): a test problem.
Not BUG-0325 (rejected commands) and not the w14 civ changes (founding replay and vehicle mirroring
unchanged; the vehicles waited for their city).

## Fix
2d79697: the campaign's census skips guests whose pause menu is open, as the sync check does.
Test `coop_civ_pause_city.py`: a city founded during a guest's pause reaches it after the pause.

## Verification
* FAIL (old campaign): lab 20261030-210914 campaign#3 (the census finding during the pause).
* PASS: the scenario test, lab run 20261030-232554-fix-w17-tribe. no-fail-run for the campaign
  change itself: needs a pause at a founding inside a full campaign.

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w17-tribe): the guest was paused; census skips paused guests; scenario test.
- 2026-11-01: released in 0.7.3.2.
- 2026-11-01 (fix/w34-vote-cinematic, BUG-0404): the mod's busy gate reads the game time's pause flag (cGameTimeManager +0x48 bit 0: the pause menu, the Sporepedia, My Collections alike); `coop.status` prints `busy:` and the campaign's sync and census checks skip a guest whose game stands by it, not only by the "Resume Game" button.
