---
id: BUG-0325
title: play_civ: a guest's gift of one vehicle did not reach the host, and one guest command was rejected (seed 11)
status: open
kind: desync
stage: Civ
reported: 2026-10-30
source: play_civ.py --seed 11, fix/w14-tribe-civ
role: guest
versions: fix/w14-tribe-civ (main b3101d2 + branch)
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
Not analysed. Code reading: the host counts a rejection for a malformed gift (bad `to`) or a
command for a vehicle / city the sender does not own; a gift of a vehicle the sender does not own
on the host is skipped silently (not counted). Candidates (inferred): the vehicle changed owner or
died on the host between the guest's selection and the gift, or a command raced a capture.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w14-tribe-civ): again in seed 5 (2 rejected guest commands; "1 own vehicles selected but no give button (My role: none - pick one)"). The host now logs every rejected command with its kind ("civ: rejected command kind K from player P"). Open.
- 2026-11-01 (fix/w31-civ-gate): play_civ on lane 9 (lab 20261101-104353, default seed): "did not happen: guest sees [8] owned by the partner and no longer selected" at 10:45:20; the host logged "Bob gave 1 vehicle to Alice" at 10:45:11 (the gift arrived) and "Alice gave 1 vehicle to Bob" at 10:45:20, the moment of the check: likely the host's bot gave the vehicle back (inferred, the vehicle id of the host's gift is not logged). No rejected command (rejected=0).
- 2026-11-01 (feat/w33-tests-session): play_civ on lane 10 (lab 20261101-134020, seed 200237565): "guest commands: sent 78, the host executed 77 and rejected 1"; the host logged `civ: rejected command kind 0 from player 1` at s 173.8 (kind 0 = an order: the vehicle was not the sender's on the host, own_vehicle() null). Faults 0, vehicle sync p90 0.47 m. The only problem of the run. Open.
- 2026-11-04 (fix/w41-civ-citizens): 4 more sightings in the November play_civ runs (lab 20261101-134020, 20261102-191952, 20261102-212559, 20261103-012624), each one rejected kind 0 (an order), none explained by the logs. The host's log now names the reason (`(vehicle N: not in the host's game)` / `(vehicle N: owner P[, dead])`) so the next sighting says whether it is a race with a death, a gift or a capture. Not fixed.

Update (2026-11-05, rel/0.7.8 integration out/lab/20261105-134715 play_civ): "guest commands: sent 68, the host
executed 67 and rejected 1" ("rejected command kind 0 from player 1" at s 254); the same PROBLEM in 5 of 35
play_civ logs kept in the worktrees (0.7.4-0.7.8). Not new in 0.7.8.
