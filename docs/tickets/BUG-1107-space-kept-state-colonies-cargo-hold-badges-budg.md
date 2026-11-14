---
id: BUG-1107
title: Space: kept state (colonies, cargo hold, badges, budget) is keyed by the display name, so two players with the same name who rejoin in the other order swap it
status: open
kind: desync
stage: Space
reported: 2026-11-14
source: audit 2026-11-14 (0.7.15) NET-2
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-14: opened.
