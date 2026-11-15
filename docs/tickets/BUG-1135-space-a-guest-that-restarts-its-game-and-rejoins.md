---
id: BUG-1135
title: Space: a guest that restarts its game and rejoins gets none of its kept badges, colonies or budget back (BUG-1107's rejoin-token key is new after a game restart)
status: fixed
kind: bug
stage: Space
reported: 2026-11-15
source: test
role: 
versions: 
fixed_in: unreleased
branch: fix/w51-space2
commits: a99b5826, 3e61f162
tests: space_badges_rejoin (RESTART=1), space_kept_key, space_tests test_rejoin_kept
signature:
docs: docs/04-netcode.md#netcode-rejoin-restart, docs/re/space.md#space-kept-key
related: BUG-1107, BUG-1001, BUG-0786, BUG-1132
switch: net.rejoin_disk
---

## Report
Found by the C5 lab run (feat/w51-space, 20261115-023819): `space_badges_rejoin` FAILS. The guest leaves, its game is
closed and started again (the test's RESTART=1 default: "a rejoin after a crash or a restart"), it joins again: the host
logs "badges: Bob's table kept: ... (fresh)" instead of sending Bob's kept badges back; the rejoined guest has the
host's badges and rank 0. The same applies to the kept colonies, the cargo hold (when the guest's own disk record,
BUG-0679, does not cover it) and the Rivals budget: everything the host keeps by `spaceext::kept_key`.

## Root cause
BUG-1107 (A10, fix/w51-net) keys the kept state by `net::Session::player_key`, the hash of the HELLO rejoin token. The
token survives a crash (the slot file) and a clean leave within the same process (BUG-1001: memory only), but a clean
leave followed by closing the game empties the slot, so the restarted game sends a fresh token: to the host it is
another game under the same name (exactly what coop_space_kept_key's `keptkey other on` simulates), and it gets nothing.
0.7.14 keyed by the session name, so a restart under the same name got its state back (and two same-name players
swapped it). Not caused by C5: the same FAIL with the hand-built paths of rel/0.7.15 (Verification).

## Fix
The orchestrator's decision (owner's rule: token-first rejoin): the guest keeps the token of a clean leave on disk per
host address and name. rejoin.cpp: `clear(true)` (a leave, a lost session) and `shutdown(true)` (the game closed in a
session, coop.cpp on_unload) also write the token into `<AppData>\SporeCoop\rejoin-<instance>-<slot>-kept.txt`, the
file of the slot this game holds exclusively; `token_for_join` takes the crash's slot token first, then the memory one
(BUG-1001), then the file's for exactly this address and name. rejoin_kept.h (portable, unit `test_rejoin_kept` in
space_tests): a random salt in the header, lines `<key> <token> <time>` with key = salted FNV-1a + finaliser of
(address, 0, name): the file holds no readable address or name; 24 h expiry (also when the clock went back), at most
16 entries (the oldest go), a damaged file is an empty store, written via a .tmp and a rename. Never sent to another
host or under another name (BUG-1001's "no stable id follows a player from host to host" stays true); another game
under the same name has another slot / instance file or a fresh token (`keptkey other`): nothing (BUG-1107).
Switch `coop.fix net.rejoin_disk off`: memory only, as before. The badges test switches the twin's fixes off in the
restarted guest too.

## Verification
- FAIL (SPORE_LAB_FIXOFF=space_badges_rejoin=net.rejoin_disk): out/lab/20261115-042154 (lane 7): after the restart
  the host logs "Bob's table kept ... (fresh)", the rejoined guest has rank 0 and misses its badge B8E0B056.
- PASS: out/lab/20261115-042151 (lane 6), out/lab/20261115-042358 (lane 7, with the rest below): no "(fresh)" after
  the restart, the guest's own badges back.
- BUG-1107 stays fixed: space_kept_key PASS in out/lab/20261115-042358 (another game named Bob gets 0, Bob's own game
  its budget). The restart-free rejoin tests PASS there too: space_items_rejoin, space_colony_rejoin,
  space_colony_crash (a kill and restart: the slot token), space_rival_merge.
- Unit: space_tests `test_rejoin_kept` (same address and name only, other port / name / host nothing, no address or
  name in the text, round trip, other salt other key, replace, expiry, bound, damaged file).
- Not run: a colony or cargo test with a clean leave plus a restart (none exists; the same token lookup serves all
  four stores, BUG-1107).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on fix/w51-space2 (net.rejoin_disk), FAIL/PASS twin with space_badges_rejoin.
