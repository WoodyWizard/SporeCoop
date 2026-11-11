---
id: BUG-0772
title: Vote: a leaver that never sent a state keeps its I'm ready flag; the next player of its id counts as ready
status: open
kind: bug
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: 
versions: all (0.7.3.0 and later)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0064, BUG-0422
---

## Report
Found by reading the code. A player who presses I'm ready in a vote and leaves before its game sent any player state
(still in the main menu, loading the host's world, or every state rejected as a bad pose) keeps its ready flag on the
host. The next player who joins gets the same id (the lowest free one) and counts as ready: the vote can pass
without them.

## Root cause
Inferred from the code (main e49e1e23), confidence medium:
* mods/coop/coop.cpp:4498-4505 (rel/0.7.12 :4613): in `g_sess.cb.player_leave` the line `g_ready.erase(id)` comes
  after `if (it == g_remotes.end()) return;`, so it runs only for a player that has a `Remote` (made by its first
  PlayerState).
* `g_ready` and `g_lobby_rtt` are not registered in the BUG-0422 registry (player_life.h `PLAYER_STATE`), so
  `player_life::forget_all` on the join of the next player of that id does not clear them either.
* The vote counts `g_ready.count(p.id) && g_ready[p.id]` over `g_sess.players()` (coop.cpp ~2130, ~2277).
Sibling of BUG-0064 (roles) and of the BUG-0422 class.

## Fix
<!-- not fixed yet -->

## Fix idea
Erase `g_ready[id]` and `g_lobby_rtt[id]` before the early return in `player_leave` (or register both maps with
`PLAYER_STATE` so join and leave clear them), behind a switch (e.g. `vote.ready_forget`).

## Verification
<!-- not fixed yet -->

## How to test
Lab test (3 instances): the host starts an action vote while guest B is in the menu (no state sent yet);
B sends `coop.ready on` and leaves; guest C joins and gets B's id; assert the host's `coop.vote` status does not
count C as ready and the vote does not pass before C presses ready. FAILS on main, PASSES with the fix.

## History
- 2026-11-10: opened.
