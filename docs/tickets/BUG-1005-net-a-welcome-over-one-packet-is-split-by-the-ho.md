---
id: BUG-1005
title: Net: a WELCOME over one packet is split by the host but dropped by the joining guest (peer_split still false before WELCOME)
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-3
role: guest
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
