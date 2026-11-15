---
id: BUG-0969
title: Tests: the unit suite's LAN discovery check (net_tests.cpp:1322) fails when another lane runs unit at the same time (fixed localhost port)
status: fixed
kind: test
stage: Network
reported: 2026-11-12
source: fix/w48-walk lab 20261112-154553 (smoke, lane 2)
role: 
versions: 0.7.13 (unreleased)
fixed_in: 0.7.13
branch: fix/w48-last
commits: 8fbf48a9
tests: unit_parallel, unit
signature:
docs:
related: BUG-0964
---

## Report
Found in fix/w48-walk's smoke on lane 2 (lab 20261112-154553): `unit` failed in tests/net_tests.cpp test_discovery
(lines 1322-1327: `found.size() == 1`, the discovery on 127.0.0.1 found no game). The same build passed `unit` in the
smoke before (20261112-150529) and in a rerun (20261112-160643). No code of the network or the test changed on the
branch.

## Root cause
Shown (2026-11-12, fix/w48-last): test_discovery (and the LAN list check of BUG-0774) hosted on the fixed port
kDefaultPort + 19 of 127.0.0.1 and asked the whole discovery range kDefaultPort .. +19 there. Two things share that
range on a lab machine: (1) another copy of net_tests (`unit` of another lane's smoke): the second copy's
`host.host()` fails on the taken port (net_tests.cpp:1308) and its discovery finds the first copy's host; (2) the games
of the other lanes, which host on kDefaultPort + 2(N-1) and answer a query to 127.0.0.1 in 10-14 ms (a probe of
47810..47829 got "Alice" answers from two running lanes): found[0] is such a game (name, info, players and port all
differ: the failure of lab 20261112-154553). Two copies started together failed in 2 of 3 pairs (scratch runs);
unit_parallel (3 copies x 2 rounds) failed 1 of 6 copies in lab 20261112-163041.

## Fix
mods/coop/net/discovery.{h,cpp}: `Discovery::ports`, the ports asked on every target (empty: the default range, as
the game uses it). tests/net_tests.cpp: both checks host on an ephemeral port (`host(0, ...)`) and ask only it; the
check also hosts an "OtherLane" session in the range during the discovery (what another lane's game is), which must
not show up. No switch (a test fix; the game's discovery is unchanged).

## Verification
New test `unit_parallel` (tools/tests/unit_parallel.py, NO_GAME): builds the net tests and runs 3 copies at once,
2 rounds; every copy must end ALL OK.
FAIL (before the fix, 03aee0a9 + the test): out/lab/20261112-163041 (copy 1 of round 1: net_tests.cpp:1308, 1322-1327).
PASS: out/lab/20261112-163220, out/lab/20261112-163355. Scratch: two copies at once 0 of 10 failed with the fix, 2 of 6
without.

## How to test
Run `tools/lab.py run unit` on two lanes at the same moment several times; a fix would host on a free port (bind
port 0, read it back) and point the discovery at it.

## History
- 2026-11-12: opened (fix/w48-walk).
- 2026-11-12: cause shown (fixed port + the lanes' games in the range), ephemeral port asked alone, unit_parallel (fix/w48-last).
- 2026-11-12: released in 0.7.13.
