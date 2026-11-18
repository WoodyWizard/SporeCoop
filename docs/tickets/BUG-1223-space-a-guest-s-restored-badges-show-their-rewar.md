---
id: BUG-1223
title: Space: a guest's restored badges show their Reward box again in the next session (space_session key frame: Explorer 2 over the galaxy view)
status: fixed
kind: test
stage: Space
reported: 2026-11-16
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: fix/w51-space3
commits: 98350224
tests: space_session
signature:
docs: docs/re/space.md#space-badges-reward, docs/06-testing.md#testing-space-session-rewards
related: BUG-1220, BUG-0786, BUG-1149, BUG-1225
---

## Report
Seen on fix/w51-visual while adding the space_session key frame (BUG-1220): in the next session (twoinst.session_restart)
the guest's game shows the badge "Reward" panel "Explorer 2 - You've explored 50 Solar Systems" (4 badge points, tools
Raise / Lower / Level Terrain) over its galaxy view, right after `badges: own badges back from the host: 144 badges (2
earned)` / "Your badges and rank are back." (space_session.guest.game.log 09:59:02.906). Frames:
fix/w51-visual out/lab/20261116-094321/frames/space_session/01_colony_guest.jpg and
out/lab/20261116-095532/frames/space_session/01_colony_guest.jpg (the panel holds the clicks: the guest's ship could not
land until it was closed). The badge was earned in the first session (`coop.space badges event 2 60`).

To reproduce: `tools/lab.py run space_session`; look at the guest's colony key frame, or `uifind.shown(guest, 'Reward')`
after the restore.

## Root cause
The inferred cause (the restore re-showing a badge) is wrong: the box was a NEW badge the test had left pending.
* observed (Ghidra GOG 2024): the Reward box comes only from `BadgeManager_GiveBadge` 0xFE58B0 (the badge UI 8 of the
  Space game gets the badge id); the restore (space_badges.cpp `write_badges`: the three maps written the Read's way,
  then AddToBadgeProgress(0xC, 0)) calls no GiveBadge. The game's Update 0xFE6320 gives an available badge (state 2/3)
  whose counters are met, and gives nothing while the box of the last one is up.
* observed (runtime lane 6, `space` fixture, 2026-11-17): `badges event 2 60` (+60 explored systems) made Update give
  B8E0B056 "Explorer 1" (15 systems) and its Reward box; B8E0B055 "Explorer 2" (50 systems) stayed at state 2 while
  that box was up; the moment the box was closed (its green check) Update gave Explorer 2 and the next box. The test
  never closed the first box (`no_movie` only confirms captions while a cinematic runs), so the guest quit with
  Explorer 2 pending (state 2, counter met); in the next session the restore gave back exactly that table and Update
  gave Explorer 2 with its box ("Explorer 2 - You've explored 50 Solar Systems" in the BUG-1220 frames; the badge
  earned in the first session was Explorer 1). The same happens in the single-player game when a save is made with
  the box up; it is not the restore.
* observed: with the boxes closed in the first session (as a player does) the next session shows no box and the
  guest's earned badges are exactly the ones it left with (lab 20261117-124203: `closed 2 reward box(es)`, `reward box
  None, earned anew []`).
* inferred: the BUG-1220 run 20261117-120207 where the guest could not land (BUG-1225) had this box over the guest's
  game during the landing tries (the box takes the clicks; `close_reward` before the frame closed it).

## Fix
Test only (no behaviour change, no switch): space_session closes every Reward box in the first session like a player
(`close_rewards`: the green check until none came for 8 s) and checks after the restore (`restored=1`) that no Reward
box comes within 15 s and that no badge is earned anew. `SESSION_REWARDS_OPEN=1` leaves the boxes open (the old run).

## Verification
* FAIL (the first box left open, the old test with the new check): 20261117-122124 (lane 6): `guest reward box (743,
  543)`, Explorer 2 given after the restore.
* PASS: 20261117-124203 (lane 6): 2 boxes closed in the first session, none after the restore, the guest landed on its
  colony (frame space_session/01_colony_guest.jpg); 20261117-125525-2 the same.

## History
- 2026-11-16: opened.
- 2026-11-17: fix/w51-space3: cause found (a pending Explorer 2 left behind the open Explorer 1 box, given by the
  game after the restore; the restore is silent); test closes the boxes like a player and checks the restore is silent.
- 2026-11-18: released in 0.7.15.
