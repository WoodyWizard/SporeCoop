---
id: BUG-1107
title: Space: kept state (colonies, cargo hold, badges, budget) is keyed by the display name, so two players with the same name who rejoin in the other order swap it
status: fixed
kind: desync
stage: Space
reported: 2026-11-14
source: audit 2026-11-14 (0.7.15) NET-2
role: host
versions: 
fixed_in: 0.7.15
branch: fix/w51-net
commits: a74f8819
tests: space_kept_key, net_tests test_player_key
signature:
docs: docs/re/space.md#space-kept-key, docs/06-testing.md#testing-kept-key-kick
related: BUG-0141, BUG-0235, BUG-1001, BUG-0195, BUG-0678, BUG-0786, BUG-1029
---

## Report
Audit 2026-11-14 for 0.7.15, finding NET-2. Not seen at runtime, found by code reading on main a762f240. Two players
with the same name (the default Spore name makes this likely) who leave and rejoin a Space session in the other order
get each other's kept colonies, cargo hold, badges and Rivals budget.

## Root cause
Observed in code (main a762f240), confidence high: the kept stores look the player up by `spaceext::name_of(id)`
(space_map.cpp:572-576, space_trade.cpp:1592-1595, space_badges.cpp:316-320, space_rivalry.cpp:608-610). The session
name is the HELLO name made unique in join order ("Player", "Player 2", session.cpp:640-656), so the suffix follows
the join order, not the player. The rejoin token that tells them apart is used only for admission (session.cpp:563-640,
BUG-1001). Related: BUG-0141, BUG-0235, BUG-1001.

## Fix idea
A stable per-player key on the host: a hash of the HELLO rejoin token (never sent on), the name only for a peer without
a token; exposed through `Session::players()` and used by every kept store. Plan package A10 (part of NET-1 / C5).

## How to test
net_tests: two equal names, swapped rejoin order, each gets its own key. Lab: a party test with two "Player" guests
rejoining in swapped order, assert each game's own hold and colonies (FAIL with the switch off).
## Fix
`net::Session::player_key(id)` (host): `t:` + a 64-bit mix of the player's HELLO rejoin token (never printed or sent),
`n:` + the session name for a peer without a token (an older SporeCoop). `spaceext::kept_key(id)` returns it, and the
four kept stores use it as their map key: space_map `g_kept` (colonies), space_trade `g_kept_inv` (hold), space_badges
`g_kept` (badges), space_rivalry `g_budget_name` (budget: K_MONEY, K_ASK, the Allies merge). The status lines show the
name kept beside the key (`kept '<name>'` as before). The same game after a crash or a leave keeps its token
(rejoin.h), so it gets its state back; another game under the name, or the same game under another name or after a
restart of the game (a new token), is a new player, as the BUG-1001 join rule already treats it.
Switch `coop.fix space.kept_token off`: the session name (0.7.14). The allies mirror (`g_m`, by name) is not kept
state handed back and stays as it is. Test seam `coop.space keptkey other on|off` (rejoin::test_other).

## Verification
`coop_space_kept_key.py` (suite `space_kept_key`, fixture space, ~2-3 min): Rivals, the guest's budget 57000; the guest
leaves, another game named Bob joins (fresh token): it must start with 0; then Bob's own game: it must get 57000 back.
* FAIL (SPORE_FIX_OFF=space.kept_token): out/lab/20261115-012724 (the other game named Bob got Bob's 57000)
* PASS: out/lab/20261115-012503-2, out/lab/20261115-013114 (the other Bob 0, Bob's own game 57000 again)
* unit: net_tests `test_player_key` (two "Player" games with different tokens time out and come back in the other
  order: each keeps its key although the names swapped).
Not run in the lab: the colonies, hold and badges stores (the same key function, the same lookup change; their own
rejoin tests space_items_rejoin, badges rejoin, colony rejoin keep one guest and are unchanged by the key).

## History
- 2026-11-14: opened.
- 2026-11-15: fixed on fix/w51-net (plan 0.7.15 A10): kept state keyed by the rejoin token.
- 2026-11-18: released in 0.7.15.
