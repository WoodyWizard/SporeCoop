---
id: BUG-0969
title: Tests: the unit suite's LAN discovery check (net_tests.cpp:1322) fails when another lane runs unit at the same time (fixed localhost port)
status: open
kind: test
stage: Network
reported: 2026-11-12
source: fix/w48-walk lab 20261112-154553 (smoke, lane 2)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## How to test
Run `tools/lab.py run unit` on two lanes at the same moment several times; a fix would host on a free port (bind
port 0, read it back) and point the discovery at it.

## History
- 2026-11-12: opened (fix/w48-walk).
