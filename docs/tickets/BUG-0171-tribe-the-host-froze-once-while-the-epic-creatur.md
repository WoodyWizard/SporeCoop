---
id: BUG-0171
title: Tribe: the host froze once while the epic creature was being killed
status: not-reproduced
kind: freeze
stage: Tribe
reported: 2026-10-22
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: host
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch: fix/w1-tribe-civ
commits: ba9dc46
tests: tribe_play, chaos_tribe, tribe_epic_dying, tribe_epic_dying_slow
signature:
docs: docs/re/tribe.md#tribe-lifetime
related:
switch: tribe.hit_dead
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* `coop_tribe_play.py` on branch feat/net-window, lane 2 (2026-10-22): the host stopped drawing
  during `coop.tribe epic hit` (monitor: FROZE); not reproduced in 14 more runs. The game logs of
  that run were lost (now kept by lab.py). Hunt: `lab.py run tribe_play,chaos_tribe --repeat 10`.

## Root cause
Not reproduced (0.7.11 N1, see Verification). Reasoning:
* The freeze came in coop_tribe_play.py's loop of `coop.tribe epic hit` (a test command, no player path): the loop hit
  with the host's and the guest's member in turn and stopped only on a reply starting at hp 0, so one or two blows
  after the lethal one went through the epic's TakeDamage at hp 0. That candidate was tried directly on GOG 2024
  with the old command (`tribe.hit_dead` off, 0.7.11): 6 epics killed, each followed by 20 more blows (the ones that
  landed at hp 0 show as "hp 0.00 -> 0.00"), also at 20 fps over the player network: both games kept drawing, 0
  faults. The other candidate, a raw `g_epic` read after the epic was freed, is closed since ba9dc46 (the object-list
  check) and by the epic tick clearing the pointer when the epic dies.
* The run of 2026-10-22 was on lane 2, a GOG 2016 lane then (docs/06-testing.md, runs of 2026-10-23), and its logs
  were lost; GOG 2016 is not supported since 0.7.8. Inferred: the freeze is not reachable on GOG 2024 by the paths
  above; players kill the epic through the game's own fights, not the test command.

## Fix
No cause found. Hardened on the way (fix/w1-tribe-civ, ba9dc46): `g_epic` (a raw pointer kept for
minutes) is checked against the game's object list before it is read, `coop.tribe epic hit` stops
at a dead victim (the test loop went on hitting a dying epic), and the epic state ends with the
session and the world.

## Verification
Hunt on main (88341cf), lane 5, GOG 2024: `lab.py run tribe_play,chaos_tribe --repeat 4`, lab run
20261027-183142: 8 of 8 passed, no freeze (the epic was killed together in every tribe_play run).
With the hardening: tribe_play in lab run 20261027-185915. 14 more runs before (2026-10-22).

0.7.11 N1 (fix/w46-n1-crashes, GOG 2024), all OK, no freeze, 0 faults:
* tribe_epic_dying (3 epics, 20 blows after each death): the fix on out/lab/20261107-231415 (lane 9); SPORE_FIX_OFF=
  tribe.hit_dead (the 2026-10-22 command) out/lab/20261107-231340 (lane 10), also tribe_epic_dying_slow (20 fps,
  player network) in the same run; tribe_epic_dying_slow with the fix out/lab/20261107-232504.
* tribe_play out/lab/20261107-232504; tribe_epic and chaos_tribe out/lab/20261107-223550; chaos_tribe_awful (5 min
  over the awful network) out/lab/20261107-233741; multi4_tribe and multi4_tribe_slow (4 players, 6 min churn)
  out/lab/20261107-223550.

## History
- 2026-10-22: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-27: not reproduced in 8 runs on main (lab 20261027-183142); epic lifetime hardened on fix/w1-tribe-civ (ba9dc46).
- 2026-10-30 (fix/w13-tribe-civ): still not reproduced. All lab game logs of the main checkout (out/lab, 2026-10-22..30) searched for tribe-stage faults: none at this ticket's address in any tribe test; the only Tribe faults in campaigns are BUG-0285 / BUG-0310 (0xC0DB23, fixed by tribe.leave_walk). On this branch tribe_coop, tribe_play, tribe_far, chaos_tribe, menu_stale_player, campaign_tribe, tribe_leave_walk (3x) passed on lane 6 without a fault, freeze or crash (lab runs 20261030-141833, 20261030-140307). Status kept.
- 2026-11-07 (fix/w46-n1-crashes, 0.7.11 N1): the old command's blows at hp 0 tried on purpose (tribe.hit_dead off, tribe_epic_dying, also at 20 fps over the player network): no freeze; stays not-reproduced, closed with the runs.
