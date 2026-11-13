---
id: BUG-1005
title: Net: a WELCOME over one packet is split by the host but dropped by the joining guest (peer_split still false before WELCOME)
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-3
role: guest
versions: 
fixed_in: unreleased
branch: fix/w49-net
commits: 226988e3
tests: unit (make test: net_tests test_welcome_split)
signature:
docs: docs/04-netcode.md#netcode-n2-join-input, docs/06-testing.md#testing-n2-net
related:
switch: net.welcome_split
---

## Report
Audit 2026-11-12 for 0.7.14, finding NET-3. Not seen at runtime, found by code reading (2026-11-13, plan/0.7.14).
If a MSG_WELCOME is longer than one reliable piece (Connection::kMaxSingle = 1163 bytes) the host splits it (it knows
the guest splits from the HELLO caps) but the joining guest drops the pieces: the join hangs and times out after 10 s.
Not reachable with the current limits: names are cut to 24 code points (<= 96 bytes) and versions to 120 code points
before they go into the roster, so the largest WELCOME of 4 players is about 900 bytes; a longer version text or a
5th player (kMaxPlayers is 4) would make it real.

## Root cause
Observed in code (main efd2e0ba), confidence high: mods/coop/net/connection.cpp:79-82 the receiver drops pieces
(`frag_drop_ = peer_caps && !peer_split`); on the guest `p.conn.peer_split` is set only when MSG_WELCOME is read
(session.cpp:705), so the WELCOME's own pieces are dropped. The host sets peer_split from the HELLO at :607, before
the WELCOME is sent at :616. Size bound inferred from protocol.h:270-291 (Welcome::write), utf8.h:106 kNameMax 24,
session.cpp:16 kVersionMax 120.

## Fix idea
The guest that announced CAP_SPLIT accepts pieces from the host it is joining (peer_split true from start_hello on
the guest side; an older host never splits). Or a static_assert / test on the largest WELCOME. Switch `net.welcome_split`.

## How to test
tests/net_tests.cpp: host and client sessions over the fake transport with a host version text of 1100 bytes
(clean_text is not applied to the host's own version): main, the client never gets welcomed (FAIL); fix: welcomed.

## Fix
Fixed, not only guarded: the bug is reachable today. The ticket's size bound assumed ASCII; names are cut to 24 code points (up to 96 bytes with 4-byte characters) and version texts to 120 code points, which str8 sends as up to 255 bytes. With a 4-player roster of 24-emoji names and 255-byte versions (a modified client, or a host whose own version text is long) the WELCOME to the third guest is about 1430 bytes against the 1163-byte piece, and that guest timed out. (Owner decision 7 said close unreachable 1005 with a guard test; since it is reachable a fix with a twin test replaces the guard.)

The guest sets `peer_split` on its connection in `start_hello` when its HELLO announced CAP_SPLIT, so it joins the pieces of the host's WELCOME; the WELCOME then sets the host's real caps. An older host never splits (it ignores the caps), and the guest's HELLO (at most about 530 bytes) is never split. Switch `coop.fix net.welcome_split off`.

## Verification
* Unit (make test): net_tests `test_welcome_split` (host + three guests over localhost with 24-emoji names and 255-byte version texts): FAIL `net_tests --off welcome_split` (the third guest never connected: out/n2-unit/net_off_welcome_split.txt in the fix/w49-net worktree), PASS in make test twice (out/n2-unit/net_pass_1.txt, net_pass_2.txt).
* No lab twin: a lab game sends its own short version text; the unit case drives the real Session over real sockets.

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
- 2026-11-13: fixed on fix/w49-net (0.7.14 N2): the joining guest joins the WELCOME's pieces; reachable with long names and versions (the guard idea dropped); FAIL/PASS unit runs.
