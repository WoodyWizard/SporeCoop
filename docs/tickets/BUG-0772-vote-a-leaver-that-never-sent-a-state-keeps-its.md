---
id: BUG-0772
title: Vote: a leaver that never sent a state keeps its I'm ready flag; the next player of its id counts as ready
status: not-reproduced
kind: bug
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: 
versions: all (0.7.3.0 and later)
fixed_in: 
branch: fix/w48-net
commits: 5857b6d1
tests: vote_leaver
signature:
docs: docs/06-testing.md#testing-n1-net
related: BUG-0064, BUG-0422
switch: none (not reproduced; the guard has no old path worth a switch)
---

## Report
Found by reading the code. A player who presses I'm ready in a vote and leaves before its game sent any player state
(still in the main menu, loading the host's world, or every state rejected as a bad pose) keeps its ready flag on the
host. The next player who joins gets the same id (the lowest free one) and counts as ready: the vote can pass
without them.

## Root cause
Inferred from the code at the audit (main e49e1e23), medium confidence: the flag outlives a leaver that never sent a
state, because player_leave erased it only after `if (it == g_remotes.end()) return;`.

CORRECTION (2026-11-12, fix/w48-net): that path is not reached. The host's (and the guests') `cb.player_join` makes the
player's Remote at once (coop.cpp `auto& r = g_remotes[p.id]` since M0), not its first PlayerState, so player_leave
always finds it and erases the flag. Observed: lab 20261112-111316 (the guard's first build with its switch off,
i.e. the 0.7.12 leave path): the scripted leaver pressed ready, left ("Leaver left" in the host log, the line after
the early return), the newcomer got its id and was NOT ready in the host's lobby table; the vote waited until it
pressed ready.

## Fix
No bug fix. A guard kept (commit 5857b6d1): the ready flags and round trips are registered in the BUG-0422 registry
(PLAYER_STATE "ready", so `coop.players held <id>` shows a kept flag) and player_leave erases them before the
Remote check. The switch tried first (vote.ready_forget) was removed: its off side is the same behaviour.

## Fix idea
Erase `g_ready[id]` and `g_lobby_rtt[id]` before the early return in `player_leave` (or register both maps with
`PLAYER_STATE` so join and leave clear them), behind a switch (e.g. `vote.ready_forget`).

## Verification
* no-fail-run: not reproduced on 0.7.12's path (lab out/lab/20261112-111316: the newcomer under the leaver's id was not
  ready with the old leave code). The lab test vote_leaver stays as a regression test: PASS out/lab/20261112-112000 and
  out/lab/20261112-112115. (Its one FAIL line in 20261112-111316, "the scripted guest left", was the test's own
  match of the bare name in coop.status, fixed to the roster row.)

## How to test
Lab test (3 instances): the host starts an action vote while guest B is in the menu (no state sent yet);
B sends `coop.ready on` and leaves; guest C joins and gets B's id; assert the host's `coop.vote` status does not
count C as ready and the vote does not pass before C presses ready. FAILS on main, PASSES with the fix.

## History
- 2026-11-10: opened.
- 2026-11-12: not reproduced (fix/w48-net): the Remote is made at join; a guard and the regression test vote_leaver.
