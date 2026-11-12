---
id: BUG-0945
title: Lab: the monitor's ground gate fails a campaign on 'the tribe members 5.62 m over the ground': which member and why is not in the logs
status: fixed
kind: test
stage: tribe
reported: 2026-11-11
source: final check of 0.7.12 (lab 20261111-151535, campaign)
role: host
versions: 0.7.12
fixed_in: unreleased
branch: fix/w47-final12c
commits:
tests: tribe_member_air (coop_tribe_member_air.py), gates_selftest (f_tribe_hang, f_tribe_toss, f_tribe_toss_old)
signature:
docs: docs/re/tribe.md#tribe-member-toss, docs/06-testing.md#testing-monitor-tribe-air
related: BUG-0946, BUG-0947, BUG-0329, BUG-0444
---

## Report
The whole campaign of the third final check of 0.7.12 (rel/0.7.12 lab 20261111-151535, lane 2, four players) passed
its own checks and failed on the lab's gate: "gate monitor: 1 on host (ground): host: the tribe members 5.62 m over
the ground (Tribe) on 2 lines from s 1630". Never in an earlier log. The message named neither the player whose
members they were nor a member, and nothing else in the four game logs did.

## Root cause
The rule was "the highest member of a player over 3 m on two trace lines in a row" (the lines are 10 s apart). It
cannot tell a member that hangs in the air from members that are in the air for a second at both samples, and the
trace had nothing to tell them apart with.

Observed in the failing run:
* The lines are the host's view of player 2's (Carol's) members: `agl_hi` 4.43, 5.62, 5.60, 6.93 m at s 1630, 1640,
  1650, 1660; 0.00 on the 13 lines before and from s 1670 on. All three guests have 0.00 for the same members at
  those marks: a guest's mirror keeps its own height (drive.own_height, BUG-0329 / BUG-0444), so only the host's game
  held somebody up.
* Carol's pointer stood at one place 110 m from the village from s 1620 to 1648 (its bot's order: a hunt), the members
  were back on the ground once the next order went out. Around it: three animals killed (s 1531, 1554, 1596), both
  Bob's and Carol's chiefs knocked out and saved (s 1521, 1558), the tribe from 19 to 17 members: hard fights.
* No terrain change on the host between s 1291 and 2948, no epic before s 1708, no body left (bodies_sent stands at
  478 from s 1620), no stage or world change. Not the 0.7.12 Tribe changes: tribe_mirror_jump / BUG-0911, the held
  items and the duet act on the guests' mirrors, BUG-0942's village removal on a guest; the reading is the host's
  own game.
Observed on two games (tribe fixture; docs/re/tribe.md#tribe-member-toss):
* A blow of 60 hp and more on a member (the combatant's TakeDamage, zero push) throws it up in the fight node
  0x05218BB3: 3.1-3.7 m after a quarter second at 5-6 m/s upwards, 4.96-5.04 m at the top, over 3 m for 1.3-1.5 s;
  20 hp does not. On the host only: the guest's mirror stays within 0.06 m of its ground (lab 20261111-182334).
* Not the hut: a wounded member walks into the hut's radius and heals on the ground (0.02 m for 25 s). Not a
  knocked-out chief: it lies on the ground for 90 s and gets up there. Not water: a member on deep water floats 15 m
  over the sea bed on the host and on the guest alike. Not ordinary play: 30 min of play_tribe on three lanes, the
  2 and 8 min Tribe plays of two four-player campaigns with the new log line: no member over 3 m once
  (labs 20261111-174925, -174928, -174931, -174030, out/lane2/campaign/20261111-174934).
Inferred (not proven for that run, its logs name no member): Carol's hunters were thrown by an animal's blows and
four samples in a row caught one of them in the air (heights of 4.4-6.9 m fit throws by harder blows than the
test's). A single such sample is in older campaigns too (rel/0.7.10 20261107-141346 s 1740: 7.85 m; fix/w47-lab-memory
20261111-002733 s 1510 and 1650: 4.8 and 5.1 m), there seen by all four games at once.
Also found on the way: BUG-0946 (the guests do not see the throw), BUG-0947 (the rejoined guest's ground).

## Fix
No change of the game's behaviour; the mod measures and the rule is precise.
* mods/coop/coop.cpp `tribe_air_watch` (4 times a second in Tribe): every tracked member more than 3 m over the
  terrain has an air time. The log names it when it starts: `tribe: member #<id> of player <p> is <m> m over the
  ground at (x y z): bt=<node> hp= dead= age= tool= item= leader= speed=<m/s> (up <m/s>) over the game's surface
  <m>` (the surface is the game's own snap point: a rock or a mound over the terrain reads about 0 there), and when it
  ends: `... is down again (or gone) after <s> s over 3 m, highest <m> m`.
* The trace line: `agl_held=<s>` after `agl_hi=` while a member is over 3 m: how long one member of that player has
  been up without a break.
* tools/tests/monitor.py, the ground rule in Tribe: over 3 m with `agl_held` of at least `agl_hold` (5 s) on two
  lines in a row: a member that stays up (hanging, floating, standing on something). Thrown members (each under 2 s)
  pass. The message names whose members, the air time and the log line to look for. `agl_hold=0` is the old rule; a
  log without the field is judged as before.

## Verification
* gates_selftest: f_tribe_hang (caught), f_tribe_toss (quiet), f_tribe_toss_old (agl_hold=0: the old rule fails it).
* tribe_member_air: FAIL 20261111-182540-at-94ca59ab (the code of 94ca59ab: no log line, no agl_held); PASS 20261111-182334 and
  20261111-182542 (a member floating for 30 s: the monitor's finding "15.18 m over the ground ... one of them up for 23 s
  without a break", allowed for the test; three blows: named, each down after 1.3-1.5 s).
* Not verified: that the campaign's four lines were throws. The next run that has them prints the member, its node
  and its air time; a member up for 5 s and more still fails the gate.

## History
- 2026-11-11: opened; the cause in that run narrowed down but not proven; log lines, agl_held and the precise rule
  (fix/w47-final12c).
