---
id: BUG-0288
title: tribe: two guests see too few of the host's objects (coverage desync) in the 4-player campaign
status: fixed
kind: bug
stage: Tribe
reported: 2026-10-29
source: final campaign lab 20261029-203821 campaign#2 lane 4
role: guest
versions: 0.7.3.0 (main 3780da0)
fixed_in: 0.7.3.2
branch: fix/w9-tribe
commits: 399bd97 2bc6ef1 0e9b31d
tests: coop_tribe_bounce_mirrors.py (suite tribe_bounce_mirrors)
signature: sync-check "t: N missing on the guest" after the host's game-over bounce back; tribe lifetime spawns_blocked grows on the guest
docs: docs/re/tribe.md#tribe-bounce-mirrors, docs/06-testing.md#testing-tribe-w9, docs/known-issues.md#issue-tribe-coverage-before-bounce
related: BUG-0172 BUG-0291
---

## Report
Final 4-player campaign (main 3780da0, lab 20261029-203821 campaign#2, lane 4, run out/lane4/campaign/20261029-203829): at 21:07:57 in tribe/play the sync check reported "Bob: too few of the host objects in tribe (Bob:coverage)" and the same for Carol. No crash; the run went on to the finale. Log tails and screenshots are in that run folder (01_desync_*, 02_desync_*). Not analysed yet.

## Root cause
Observed (campaign logs + lab, lane 8):
* In the campaign the host's player tribe was knocked out (members 13 -> 5, then the game-over
  caption's cinematic, input lock 1); at 22:08:05 (game clock) the host bounced back (Tribe ->
  Loading -> Tribe, same world, new member ids t20..). From then on every guest missed 16 of the
  host's members ("sync-check: MISMATCH ... t: 16 missing on the guest" on Bob, Carol and Dave).
* Reproduced on lane 8 with 2 players (tribe fixture, `coop.tribe pincer` knocks the player tribe
  out, OK on the host): the guest's game keeps 0 members in its player tribe, `spawns_blocked` grows
  every snapshot. The guest's game shows no caption (its input lock stays 0).
* The guard of BUG-0172 (`player_tribe_out`: no member of the player tribe with health = the tribe
  is out, add nothing) also ran on the guest, whose player tribe holds mirrors only: all mirrors had
  played their death, then were removed (an empty tribe also has no member with health), so no new
  mirror was ever spawned.
Inferred / open: the campaign's finding itself (21:07:57 lab clock) came ~8 s before the bounce
back, while the tribe was being knocked out; which kind was missing then is not in the logs (the
same guard can refuse a NEW member while every mirror is dead). docs/known-issues.md#issue-tribe-coverage-before-bounce.

## Fix
`player_tribe_out` (mods/coop/tribe.cpp): on a guest the guard holds only while its own game has
the input lock up (a caption or cinematic, coop_ui::input_lock_now); the host keeps the guard.
Counter `mirror_out_passed` in the `tribe lifetime` line. Switch: `coop.fix tribe.mirror_out off`.
Tooling: the campaign's coverage finding now lists the missing keys per kind (coop_campaign.py,
coop_multi.measure `missed`).

## Verification
`coop_tribe_bounce_mirrors.py` (suite `tribe_bounce_mirrors`, lane 8, worktree out/lab):
* FAIL with the fix off (`SPORE_FIX_OFF=tribe.mirror_out`): lab run 20261030-002321: the guest's
  game has 0 of the host's 3 members, 3 of 7 t keys missing.
* PASS with the fix: lab run 20261030-002546 (2 runs, cold and warm): the guest has the host's
  player tribe again, 0 missing (also 20261030-002013 with the test before the caption check).

## History
- 2026-10-29: opened.
- 2026-10-30: root cause (the BUG-0172 guard on a guest's mirror tribe), fix and test on fix/w9-tribe.
- 2026-11-01: released in 0.7.3.2.
