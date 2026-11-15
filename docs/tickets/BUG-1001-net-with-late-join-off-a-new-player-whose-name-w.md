---
id: BUG-1001
title: Net: with late join off a new player whose name was seen in the session (the default Player) joins as a rejoin, and a same-name join while that player is silent goes over the player limit
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-1
role: host
versions: 
fixed_in: 0.7.14
branch: fix/w49-net
commits: 226988e3
tests: unit (make test: net_tests test_join_seen); lab join_late_names (coop_join_late_names.py); regression settings_creature
signature:
docs: docs/known-issues.md#issue-late-join-seen-name, docs/04-netcode.md#netcode-n2-join-input, docs/06-testing.md#testing-n2-net
related:
switch: net.join_seen
---

## Report
Audit 2026-11-12 for 0.7.14, finding NET-1. Not seen at runtime, found by code reading (2026-11-13, plan/0.7.14).
With the host's session settings "late join" off (all.late_join, F3 BUG-0768) and the rejoin token on (net.rejoin_token,
the default), a new player is let in after the start when its name was seen in this session: two friends who kept the
default name "Player" (coop.cpp:146) are enough, the second joins as "Player 2" although new players are refused.
With a player limit below 4 (kPlayerLimit), a join with the name of a joined player that is silent for 2 s (a load)
but has another token is not counted against that entry and is accepted: one player over the limit.

## Root cause
Observed in code (main efd2e0ba), confidence high:
* mods/coop/net/session.cpp:527 `by_token` is true by default; :554-555 `seen = dropped_by_token || find(seen_names_, h.name)`:
  with the token rule on, "seen" still accepts any name of the session, so :564 skips REJ_STARTED for a new game that
  only shares the name (the token of a real crash rejoin is already handled by :529-538).
* :556-560 a silent (> 2000 ms) entry of the same name is left out of `here` when `seen`, but the name rule that
  would retire that entry runs only `if (rejoin_by_name && !by_token)` (:582): with the token rule the old entry stays
  and the new one is added: players() > max_players.
* :602-603 stores `p.name` (after the " 2" suffix or "guest<id>" for an empty name) while :555 compares the raw
  `h.name`: a player with an empty name is never "seen" (a clean leave and rejoin after the start is refused).
Inferred: the "seen name" exception was written for the 0.7.3.0 name-and-silence rule (no token) and was kept when the
token rule (BUG-0235) took over.

## Fix idea
With by_token: "seen" only by `dropped_by_token`, or by a name seen AND a token equal to the token that name had
(keep the token per seen name), never by the name alone; count every accepted entry in `here` unless it was dropped
by token. Store and compare the same form of the name (the cleaned h.name). Switch `net.join_seen_token`.

## How to test
tests/net_tests.cpp (session over the fake transport, like test_halfopen): host with join_closed() = true after a
first HELLO "Player" (token A), a second HELLO "Player" (token B) after the start: main accepts it (FAIL), the fix
rejects REJ_STARTED. Second case: max_players 2, guest "Bob" silent 2.1 s, HELLO "Bob" token B: main 3 players
(FAIL), the fix REJ_FULL. In game: a coop_join_* test with three instances, late join off, two guests named Player.

## Fix
The owner's rule (plan 0.7.14 decision 7). Session::rejoin_known (session.cpp): with the token rule on and a token in the HELLO, a player may come back while join_closed() only by that token (every token admitted since host() is kept in seen_tokens_, and no live entry holds it now); a crash rejoin still replaces its silent entry by token first (BUG-0235). Without a token (an older SporeCoop, or net.rejoin_token off): by its HELLO name only when no live entry has that name and the name dropped within kRejoinNameMs (10 min, dropped_names_ written in drop_peer). A name alone never passes. The player limit counts every live entry; a silent entry of the same name is left out only when the legacy name rule (net.rejoin_token off) retires it in the same HELLO. Names are compared in one form: Peer::hello_name, the cleaned HELLO name before " 2" / "guest<id>" (the empty-name case of the ticket).

So that a player who left can still come back as itself with late join off (coop_settings_creature step 3), rejoin::clear(true) on a leave and on a lost session keeps the token in memory for the same host address string and the same name (up to 8 pairs, never in the file, never sent to another host or under another name); rejoin::token_for_join(instance, host, name) uses it. Another name from the same game is a new player. Switch coop.fix net.join_seen off: the seen-name rule before (and the token is not kept).

## Verification
* Unit (make test): net_tests test_join_seen (two games named Player with other tokens after the start; a crash rejoin by token; limit 2 with a silent Bob and another Bob; no-token names: a second one while one is here, one after its drop, one dropped over 10 min ago). FAIL net_tests --off join_seen (5 failed checks, out/n2-unit/net_off_join_seen.txt in the fix/w49-net worktree), PASS in make test twice (out/n2-unit/net_pass_1.txt, net_pass_2.txt).
* Lab join_late_names (3 games): FAIL with SPORE_FIX_OFF=net.join_seen out/lab/20261113-032627-fix-w49-net (the extra game named Bob admitted as "Bob 2" after the start, host 3 players); PASS out/lab/20261113-032442-fix-w49-net and out/lab/20261113-032630-fix-w49-net (refused with "does not take new players", Bob leaves and comes back by its kept token, late join on: "Bob 2" joins).
* Regression: settings_creature (late join off: a new name refused, the old name comes back after a leave) PASS out/lab/20261113-032442-fix-w49-net.

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
- 2026-11-13: fixed on fix/w49-net (0.7.14 N2): rejoin after the start by token (kept for the same host and name on a leave), without a token by a name nobody has that dropped recently; every live entry counts; FAIL/PASS unit and lab runs.
- 2026-11-14: released in 0.7.14.
