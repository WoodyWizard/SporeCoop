---
id: BUG-1175
title: civ_owners_reload: the lab's sync-check gate finds vehicles of the cities founded during the test missing on the guest (rel/0.7.15 too)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-15
source: lab
role: guest
versions: 0.7.14, 0.7.14.1
fixed_in: unreleased
branch: fix/w51-civveh
commits: b497d7ba
tests: civ_specialty_runs, civ_owners_reload
signature:
docs: docs/re/civ.md#civ-specialty-runs, docs/06-testing.md#testing-civ-specialty-runs
related: BUG-0982, BUG-0331, BUG-0319, BUG-0380, BUG-0950, BUG-0647
---

## Report
civ_owners_reload's own checks pass (RESULT: OK), but the lab's sync-check gate fails it: "MISMATCH with the host ...:
v: 1-2 missing on the guest" about 45 s into the session, right after the host founded two cities ("the host has 6
cities, this game 4 and 6 founded (on their way)"). The missing vehicles are those of the new cities: the guest asks the
host for their design ("creations: asked the host for the vehicle design F0F41CEC") only when its own copies of the
cities appear, a moment before or after the check.
Seen on feat/w51-civ (lab 20261115-174639, 20261115-190127, 20261115-192303 x2) and on rel/0.7.15 9362bcdd without
any change of that branch (out/lab/20261115-192303-base9362 x2, copied from a worktree of 9362bcdd): not a change of
feat/w51-civ. An earlier pass: feat/w51-store 20261115-140056 (the check ran before the host's new vehicles existed).

## Root cause
Not a race of the test and not a founding players cannot do: a real guest bug since 0.7.14 (BUG-0982).
Observed (rel/0.7.15 9362bcdd, lab 20261115-192303-base9362, both runs; v0.7.14.1 lab 20261115-203353 in the worktree
of the tag, 2 of 2 the same): the guest bought city 3, the host sent it the specialty question; the guest's game showed
the game's own box (cCity_ShowSpecialtyBox), which pauses the game (Pause 0x4BF38A7; the guest's trace `busy=paused`,
`coop.pauses` `04BF38A7=1`). The host's game founded the next site after each purchase by itself (a tribe village
destroyed, ApplyStamp of the new city at 34.0 / 37.7 s, about 20 s after the buys). The guest replayed both foundings
("the host founded a city: here founded") but the cities appear through a timed callback of the game time
(docs/re/civ.md 9.3), so they waited ("the host has 6 cities, this game 4 and 6 founded (on their way)"); without
their civilization the new nation's vehicles had no mirror: sync-check "v: 2 missing on the guest (v5 v6)" at 45 s.
The test answered the box 16 s after it opened (frame_city); 3 s later the cities appeared on the guest (ApplyStamp
49.7 s) and it asked the host for the new nation's vehicle design. v0.7.13 (the box on the host, the host's game
paused, so its founding waited too) passes 2 of 2 (lab 20261115-203924, worktree of v0.7.13).
What a player sees: while a guest reads the "choose the city's specialty" question after a purchase or a capture, its
world stands (the host's goes on): new cities and their vehicles, wild creatures (`n: 2 apart` in one run) catch up
only after the answer.

## Fix
The guest counts the box's pauses (0x4BF38A7) before and after ShowSpecialtyBox and resumes the difference at once;
the answer does not resume again (a count under 0 would swallow a later pause). The box stays up and its answer works
as before; the guest's world goes on behind it like the host's. Logged "the specialty box of city N took 1 pause(s):
released, the world goes on (BUG-1175)"; `coop.civ tables` `specialty: ... unpaused=N`.
Switch: `coop.fix civ.specialty_runs off` (the guest's game stands until the answer, 0.7.14). No wire change.

## Verification
* New test civ_specialty_runs (the guest's box up, the host founds the next site; the guest's game must show the new
  city and the new nation's vehicle mirrors under the box): FAIL with SPORE_FIX_OFF=civ.specialty_runs, lab
  20261115-203533 (guest 4 cities, the new nation's vehicles host 1 guest 0, `paused=1 04BF38A7=1`); PASS lab
  20261115-203410 and 20261115-203727 (x2).
* civ_owners_reload: FAIL 2 of 2 on rel/0.7.15 9362bcdd (lab 20261115-192303-base9362) and on v0.7.14.1 (lab
  20261115-203353); PASS 2 of 2 with the fix (lab 20261115-203727), gate included.
* civ_buy_city 2 of 2 PASS (lab 20261115-203727).

## History
- 2026-11-15: opened.
- 2026-11-15 (fix/w51-civveh): the guest's specialty box paused its game: cities founded meanwhile and their vehicles waited for the answer; fix civ.specialty_runs, test civ_specialty_runs.
