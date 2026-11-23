---
id: BUG-1399
title: tribe_bounce_world and five more tests: back-to-back blows on every member fell two in one tick, no game over (BUG-1337 class)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-21
source: final check 20261121-003202
role: host
versions: 0.7.7,0.7.16
fixed_in: 0.7.16
branch: fix/w62-saves
commits: c0792a81
tests: tribe_bounce_world, tribe_bounce_mirrors, tribe_bounce_goal, tribe_attack_stale, wsync_transitions, realign_tribe (coop_realign_planet.py), tribe_gameover* (coop_tribe_gameover.py, twin tribe_gameover_reload2_sameframe)
signature: FAIL the game-over caption is up on the host; FAIL the host's reloaded world lacks the tribe formed after its save (the scenario happened)
docs: docs/re/tribe.md#tribe-gameover-list
related: BUG-1337, BUG-1083, BUG-0564
---

## Report
Third final check of 0.7.16 (lab 20261121-003202, lane 6, warm): tribe_bounce_world `FAIL the game-over caption is up
on the host` (75 s of waiting), then `FAIL the host's reloaded world lacks the tribe formed after its save (the scenario
happened)`: the host's game never reloaded, so the world after "the bounce" was the world before it (6 tribes, food
346). Passed in 20261120-133703 and 20261120-041304. The suspicion was a lost save (BUG-1365's switch).

## Root cause
* Observed (tribe_bounce_world.host.game.log): no save in the test at all after the join's share save (s 1.8-4.4), so
  `world.save_allowed` played no part; no stage change (no Tribe -> Loading), no game-over caption. The chief rescue
  ("Bob saved Alice's chief" / "Alice saved Bob's chief", s 30.75) came as in both PASS runs, where the reload followed
  15 s later; here nothing followed. The player tribe at (406 -5 303) went from 3 members to 1 (still listed).
* Observed (the test): its blows were the old ones of BUG-1337, every member with health a blow of 300 back to back
  every second. The game posts its game over only when fewer than 2 members are left in its up list and a fallen member
  leaves that list at its next behaviour tick: two members falling in one tick give no game over at all
  (docs/re/tribe.md#tribe-gameover-list). BUG-1337 changed coop_tribe_gameover.py only; tribe_bounce_world and five
  other tests copied the old loop.
* Observed (lab logs of other worktrees): the same "FAIL the game-over caption is up on the host" of tribe_bounce_world
  on 2026-11-04 (fix/w43-realign 20261104-204645, -205014, -205650, -210008, -210244; fix/w43-world-state
  20261104-212104): an old flake of the test (0.7.7), not new in 0.7.16 and not a w61 regression.
* Inferred: the member counts and the missing caption match BUG-1337's signature; the test printed no member list
  then, so the last blow pair is not seen directly (knock_out prints `player tribe now` from now on).

## Fix
Tests only: one shared method `coop_tribe_gameover.knock_out(host, secs, dmg, same_frame)`: one blow a second on one
member at a time, the tribe's leader last, prints the tribe at the end (the BUG-1337 loop moved there unchanged,
coop_tribe_gameover.py's own --same-frame twin goes through it). tribe_bounce_world, tribe_bounce_mirrors,
tribe_bounce_goal, tribe_attack_stale, wsync_transitions and coop_realign_planet use it instead of their own back-to-back
loop. The checks are unchanged.

## Verification
Was waiting for the lab run (no lab runs today, owner rule). Natural FAIL runs of the old loop: 20261121-003202
(tribe_bounce_world), the 2026-11-04 runs above. Lab: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_bounce_world
tribe_bounce_mirrors tribe_bounce_goal tribe_attack_stale wsync_transitions tribe_gameover_reload2
tribe_gameover_reload2_sameframe --lane N --lane M --retry 0 --no-build`: all PASS except
tribe_gameover_reload2_sameframe, which must FAIL (no caption: the race made sure).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): tribe_bounce_world, tribe_bounce_mirrors, tribe_bounce_goal, tribe_attack_stale, wsync_transitions, realign_tribe and tribe_gameover_reload2 PASS in 20261122-194210 and 20261123-023643; the twin tribe_gameover_reload2_sameframe FAIL in 20261122-191140 (no caption). Closed.

## History
- 2026-11-21: opened (fix/w62-saves): not a save problem; the BUG-1337 blows as one shared method in the six tests.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
