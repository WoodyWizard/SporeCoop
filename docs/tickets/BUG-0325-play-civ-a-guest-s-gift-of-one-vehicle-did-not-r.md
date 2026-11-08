---
id: BUG-0325
title: play_civ: a guest's gift of one vehicle did not reach the host, and one guest command was rejected (seed 11)
status: fixed
kind: desync
stage: Civ
reported: 2026-10-30
source: play_civ.py --seed 11, fix/w14-tribe-civ
role: guest
versions: fix/w14-tribe-civ (main b3101d2 + branch)
fixed_in: 0.7.11
branch: fix/w46-v2-civ-space
commits: eb1abf07
tests: play_civ
signature: play_civ "did not happen: <who> sees [ids] owned by the partner"; "spice node ... guest sees FFFFFFFF"; "guest commands: ... rejected 1"
docs: docs/06-testing.md#testing-play-civ-checks
related: BUG-0206
---

## Report
`SPORE_LANE=7 python3 tools/tests/play_civ.py --minutes 10 --seed 11` (fix/w14-tribe-civ, 2026-10-30,
log out/w14/play_civ_fix_11.log + both game logs of the worktree, not in git): RESULT FAIL with two
problems: "16:06:32 did not happen: guest sees [9] owned by the partner and no longer selected"
(the guest's bot gave vehicle 9 to the host in the co-op window; 4 s later the guest still had it
as its own) and "guest commands: sent 66, the host executed 65 and rejected 1". The vehicle sync of
the run was good (p90 0.22 m, max 1.52 m). Seen once (the same seed's run before the branch had
neither).

## Root cause
Three signatures of play_civ's "no problems", each read from the logs (observed), none a desync a player sees:
* "did not happen: <giver> sees [ids] owned by the partner and no longer selected": the gift arrived on the host every
  time ("Bob gave 3 vehicles to Alice" at 12:50:51.93, rel/0.7.10 lab 20261107-124806); the check was looked at only
  at the next audit (every 8 s plus the audit's own time), and by then the partner's bot had given a vehicle back
  ("Alice gave 1 vehicle to Bob" 12:50:58.99; the same in fix/w46-tribe-civ 20261107-083217 1.4 s after the gift and
  rel/0.7.5 20261104-075207 2.7 s after): the giver rightly owned it again.
* "spice node (x, y, z): host owner P, guest sees FFFFFFFF" (the 0.7.10 final check, rel/0.7.10 lab 20261107-141346, and
  2 more runs): the claim check compared the guest at once when it first saw the host's claim; the host sends the node
  owners once a second (every 10th snapshot, civ.cpp host_tick) and a guest in a city planner takes them only back in
  Civ. The audit's own node compare (3 s second look) never found a lasting difference in these runs.
* "guest commands: sent N, the host executed N-1 and rejected 1": every logged rejection was `kind 0` (an order) with
  no reason in the log, i.e. not own_vehicle() (that one names it since fix/w41-civ-citizens): the order's target did
  not resolve on the host (resolve_target: a vehicle id the host no longer has, or an object not within 10 m), a target
  that died on the host while the guest's copy was still there (inferred: the log did not name the target).

## Fix
Test (play_civ.py): the giver polls its gift for 4 s at once (0.2 s) and compares what it sees with the host's owner of
that moment (a vehicle given back is no problem: `give_returned`); the claim check looks again 4 s or more after it first
saw the host's claim (`claim_seen_early`) before it calls a difference a problem. Diagnostics (civ.cpp host_command): a
rejected order names its target ("target kind K id I noun N at (x y z) not in the host's game"), so the next rejection
says which target it was. No mod behaviour changed, no switch.

## Verification
no-fail-run: test harness timing (the checks' look, not the game): the causes are in the logs above; play_civ with the
new checks: out/lab/20261107-220353 (lane 10, 10.3 min: no problems, 247 checks, 19 gifts, guest commands 80 sent, 80
executed, 0 rejected).

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w14-tribe-civ): again in seed 5 (2 rejected guest commands; "1 own vehicles selected but no give button (My role: none - pick one)"). The host now logs every rejected command with its kind ("civ: rejected command kind K from player P"). Open.
- 2026-11-01 (fix/w31-civ-gate): play_civ on lane 9 (lab 20261101-104353, default seed): "did not happen: guest sees [8] owned by the partner and no longer selected" at 10:45:20; the host logged "Bob gave 1 vehicle to Alice" at 10:45:11 (the gift arrived) and "Alice gave 1 vehicle to Bob" at 10:45:20, the moment of the check: likely the host's bot gave the vehicle back (inferred, the vehicle id of the host's gift is not logged). No rejected command (rejected=0).
- 2026-11-01 (feat/w33-tests-session): play_civ on lane 10 (lab 20261101-134020, seed 200237565): "guest commands: sent 78, the host executed 77 and rejected 1"; the host logged `civ: rejected command kind 0 from player 1` at s 173.8 (kind 0 = an order: the vehicle was not the sender's on the host, own_vehicle() null). Faults 0, vehicle sync p90 0.47 m. The only problem of the run. Open.
- 2026-11-04 (fix/w41-civ-citizens): 4 more sightings in the November play_civ runs (lab 20261101-134020, 20261102-191952, 20261102-212559, 20261103-012624), each one rejected kind 0 (an order), none explained by the logs. The host's log now names the reason (`(vehicle N: not in the host's game)` / `(vehicle N: owner P[, dead])`) so the next sighting says whether it is a race with a death, a gift or a capture. Not fixed.

Update (2026-11-05, rel/0.7.8 integration out/lab/20261105-134715 play_civ): "guest commands: sent 68, the host
executed 67 and rejected 1" ("rejected command kind 0 from player 1" at s 254); the same PROBLEM in 5 of 35
play_civ logs kept in the worktrees (0.7.4-0.7.8). Not new in 0.7.8.

- 2026-11-07 (rel/0.7.10 integration): play_civ lane 4 (lab 20261107-124806): "did not happen: guest sees [1, 5, 6] owned by the partner and no longer selected"; still open, in plan-0.7.11 V2.
- 2026-11-07 (fix/w46-v2-civ-space, 0.7.11 V2): the three signatures traced to the checks' timing (logs above); play_civ's gift and claim checks look in time, a rejected order names its target. Closed.
- 2026-11-08: released in 0.7.11.
