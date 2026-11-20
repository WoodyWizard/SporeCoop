---
id: BUG-1336
title: Cell -> Creature: the first Creature editor's vote never waits for a guest still in Cell, the party no longer evolves together (BUG-1245 regression)
status: open
kind: bug
stage: Cell, Creature
reported: 2026-11-20
source: lab
role: 
versions: 0.7.15.2, 0.7.15.3, 0.7.15.4, 0.7.15.5, 0.7.16
fixed_in:
branch: fix/w59-cell
commits: d04d6a10
tests: leave_avatar_cell, leave_avatar_cell_wait_off, legless_guest, parts_transition, choice_world, evo_screens_switch, evo_screens_switch_slow, core_tests arrival_wait
signature: host log "vote: 1 player(s) on their way to the Creature stage: not waited for ... (BUG-1245)" 0.0 s after the vote start; test "its first editor waits for the vote" FAIL
docs: docs/09-coop-design.md, docs/06-testing.md#testing-arrival-vote-kin
related: BUG-1245, BUG-1240, BUG-1239, BUG-1269, BUG-0315
---

## Report
0.7.16 final check out/lab/20261119-214823 (lane 6): leave_avatar_cell and legless_guest FAIL "its first editor
waits for the vote" and "the guest follows"; parts_transition "the guest plays Creature with an avatar"; choice_world
"the guest plays a creature for most of the 45 s". Triage package A (out/TRIAGE-0.7.16.md). Orchestrator decision
(2026-11-20): a timing regression, not wanted: the party keeps evolving together in the first Creature editor vote.

## Root cause
Observed (host logs): the first Creature editor's vote (the landfall editor FD4902BD after the switch vote, BUG-0315)
starts 0.3 s after the host enters Creature and ends in the same tick: `1 player(s) on their way to the Creature stage:
not waited for, their own editor opens when they arrive (BUG-1245)`, `ready 1/2 after 0.0 s`. The guest is always on
its way then: the host's share of its Creature world starts only after the host's entry (`saving for the share` 1.8 s
after the vote's start, received 4.4 s, the guest in Creature 9.6 s after it). So BUG-1245 (0.7.15.2) never waits in
any switch, not only on the slow link it was made for; the guest gets its own arrival editor alone (BUG-1240) ~15 s
after arriving, and the tests' "guest follows" came after their 30 s wait for `vote active`, when the guest was in that
editor. 0.7.15 (20261117-154911, before BUG-1245): `player 1 arrived in the Creature stage: ready`, `ready 2/2 after
9.5 s`. Not BUG-1269: its 4 s later host entry does not change the order (the four tests had not run between 0.7.15
and the 0.7.16 final check). choice_world is BUG-1240's: the guest is in its own Cell editor (BUG-1239: never waited
for) and gets its own first Creature editor after the arrival, which the test did not expect.

## Fix
`core.arrival_wait` (vote.cpp, rule `arrival_rule::waits_for` in arrival_rule.h, `world::share_in_flight`): a guest on
its way is waited for while its arrival is quick: its share not out yet (`needs_world`) or still saving / sending /
unacknowledged within 8 s of the vote, or acknowledged within 40 s of being asked for (the guest loads). Not waited
for: a guest in its own editor (BUG-1239) and one whose share is still on the wire after 8 s (BUG-1245's slow link:
the host's black picture behind the vote stays ~8 s plus the editor's opening, under evo_screens_switch_slow's 20 s).
The host logs `N player(s) arriving in the Creature stage: waited for (BUG-1336)`; `coop.vote` counts the votes that
waited. Off: BUG-1245 as before. choice_world now leaves the guest's own arrival editor with the red cross before its
samples (the BUG-1240 behaviour is wanted there). Unit test core_tests arrival_wait.

## Verification
Lab not run yet (orchestrator: no lab until "lab ok", the final check runs on other lanes); commands below.
* FAIL runs (before the fix): out/lab/20261119-214823 leave_avatar_cell, legless_guest, parts_transition, choice_world.
* PASS expected: `SPORE_DETECTOR=20 python3 tools/lab.py run leave_avatar_cell legless_guest --lane N --retry 0
  --no-build` and `SPORE_DETECTOR=20 python3 tools/lab.py run parts_transition choice_world --lane M --retry 0 --no-build`
* twin, FAIL expected: `SPORE_DETECTOR=20 python3 tools/lab.py run leave_avatar_cell_wait_off --lane N --retry 0
  --no-build` ("its first editor waits for the vote")
* regression (BUG-1245 / BUG-1240 / BUG-1269 tests): `SPORE_DETECTOR=20 python3 tools/lab.py run evo_screens_switch
  evo_screens_switch_noready evo_screens_switch_slow evo_screens_switch_lowfps --lane N --retry 0 --no-build`
  (the slow one: the host's screen not black 20 s; the others: both editors, each its own creature).

## History
- 2026-11-20: opened.
