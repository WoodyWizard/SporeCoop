---
id: BUG-1369
title: Votes: an action vote (stage switch, superweapon) passes without a guest that is in its own editor; BUG-1239's rule was meant for the evolution vote only (vote_net_4p)
status: open
kind: bug
stage: All
reported: 2026-11-20
source: lab 20261120-133703
role: host
versions: 0.7.15.1 and later
fixed_in:
branch: fix/w61-multi
commits:
tests: vote_net_4p, vote_net_4p_editor_off (twin)
signature:
docs: docs/09-coop-design.md
related: BUG-1239, BUG-0447
---

## Report
Nightly of the second 0.7.16 final check (lab 20261120-133703, lane 4): `vote_net_4p` failed two checks of its step 2
"leaver": `[leaver] the host's, Bob's and Carol's windows show ready 3/4: {'Alice': None, 'Bob': None, 'Carol': None}`
and `[leaver] the vote waits for Dave, who edits`. Steps 1, 3 and 4 passed. The test had not run since 2026-11-02
(lab 20261102-163954 PASS); it was not in the first final check (20261120-041304).

## Root cause
Mod (a fix's scope). Observed in the host's log of that run: `vote: start (action) 'Alice wants to test an action'
players=4`, then `vote: 1 player(s) in their own editor: not waited for (BUG-1239)`, Bob's and Carol's ready, and
`vote: end: everybody is ready, ready 3/4 after 3.9 s` 5 s before Dave left. The test's windows read no vote any more
(it had ended), hence `None`.
BUG-1239 (0.7.15.1, core.vote_guest_editor) was written for the evolution vote: its commit, its switch text and its
ticket name "the host's evolution vote", where the game holds the host's editor request behind a faded (black) picture
and the guest in its own editor "evolves already". Its check in vote_tick() ran for every vote, also for the action
votes of start_action_vote(): the Cell -> Creature switch vote, the Civ superweapon vote, `coop.vote action`. Nothing on
the host waits behind an action vote (the host plays on, no black screen), and the action changes the game of the
guest in the editor too, so the vote should keep waiting for its ready, its leave or the 60 s, as vote_net_4p has
asserted since 0.7.3 (BUG-0447) and as every version before 0.7.15.1 did. Inferred: nobody saw it because the only
test of an action vote with a guest in its editor (vote_net_4p) did not run between 0.7.15.1 and this nightly.

## Fix
vote.cpp vote_tick: `core.vote_editor_evo_only` (on): a guest in its own editor is skipped only by a vote for an
editor (no g_vote_action); an action vote waits for it. Off: every vote skips it (0.7.15.1). BUG-1239's case (the
host's Cell / Creature evolution vote, the arrival vote) is unchanged. docs/09-coop-design.md 9.3 says which vote.
CHANGELOG [Unreleased] Fixed has the player line.

## Verification
Open, waiting for the lab run.
* FAIL (the behaviour the switch off restores): out/lab/20261120-133703 vote_net_4p (the action vote ended 3/4 while
  Dave was in his editor).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run vote_net_4p --lane N --retry 0 --no-build` (4 games).
* Wanted FAIL (twin): `SPORE_DETECTOR=20 python3 tools/lab.py run vote_net_4p_editor_off --lane N --retry 0 --no-build`
  (`SPORE_FIX_OFF=core.vote_editor_evo_only`: the two [leaver] checks fail as in 20261120-133703).
* BUG-1239's own test should still PASS: `evo_screens_cell_both` (an evolution vote).

## History
- 2026-11-20: opened (as a test ticket: the test seemed to assert the 0.7.15 behaviour).
- 2026-11-20 (fix/w61-multi): BUG-1239's commit, switch and ticket scope it to the evolution vote; the action vote
  skipping is that fix's side effect. Restricted with core.vote_editor_evo_only, twin vote_net_4p_editor_off.
