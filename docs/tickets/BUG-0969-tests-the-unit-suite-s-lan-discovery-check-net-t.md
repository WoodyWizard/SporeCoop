---
id: BUG-0969
title: Tests: the unit suite's LAN discovery check (net_tests.cpp:1322) fails when another lane runs unit at the same time (fixed localhost port)
status: fixed
kind: test
stage: Network
reported: 2026-11-12
source: fix/w48-walk lab 20261112-154553 (smoke, lane 2)
role: 
versions: 
fixed_in: unreleased
branch: feat/lab-speed
commits:
tests: unit
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
Inferred, not proven: the test hosts on the fixed port kDefaultPort + kDiscoverPorts - 1 of 127.0.0.1 and asks it from
the same machine; other lanes' smokes (other branches) ran `unit` at the same time, and two hosts on one fixed local
port / their discovery answers get in each other's way. Not looked into further (not this branch's task).

## Fix
test_discovery (tests/net_tests.cpp) hosts on the highest free port of the discovery range (kDefaultPort + 19 down;
the lanes' games host from the bottom, +2(N-1)) under a name of its own process ("LanHost<pid>") and wants exactly one
game of that name on that port. Two net_tests at once (other lanes' units, and since feat/lab-speed the 8 parallel
shards of `make test` in one tree, three binaries at once) no longer take each other's port or answers. Test-only, no
switch (no mod code changed).

## Verification
no-fail-run: a test-only race, not reproduced on demand; `make test` (net_tests, net_tests_asan and its --nofix run,
8 shards each, all at once) passed repeatedly on feat/lab-speed.

## How to test
Run `tools/lab.py run unit` on two lanes at the same moment several times; a fix would host on a free port (bind
port 0, read it back) and point the discovery at it.

## History
- 2026-11-12: opened (fix/w48-walk).
- 2026-11-12: fixed on feat/lab-speed (free port, own name).
