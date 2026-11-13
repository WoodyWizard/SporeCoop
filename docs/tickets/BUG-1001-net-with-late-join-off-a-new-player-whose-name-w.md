---
id: BUG-1001
title: Net: with late join off a new player whose name was seen in the session (the default Player) joins as a rejoin, and a same-name join while that player is silent goes over the player limit
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-1
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-late-join-seen-name
related:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
