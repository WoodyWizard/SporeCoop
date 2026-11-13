---
id: BUG-1010
title: diag: net last_silent mixes GetTickCount with the steady_clock connection time, the leave lines print a garbage last heard value
status: open
kind: tooling
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-6
role: 
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
Audit 2026-11-12 for 0.7.14, finding NET-6. Not seen at runtime, found by code reading (2026-11-13, plan/0.7.14).
The "last heard N ms before the last check" value in the player-left / session-ended log lines and the session summary
is garbage (two clocks with other epochs subtracted). Diagnostics only: a report reader is misled about a timeout.

## Root cause
Observed in code (main efd2e0ba), confidence high: mods/coop/diag_stats.cpp:174 `nowms = GetTickCount()`, :186
`last_silent = nowms - c->last_recv_ms`; last_recv_ms comes from net::now_ms() = steady_clock (net/udp.cpp:45-49,
QPC-based on MSVC / Wine), not GetTickCount. Printed at :497, :601-604, :612-614.

## Fix idea
Use the session's clock (`g_P.sess->now()` or net::now_ms()) in net_second, or Session::silent_ms (session.cpp:329). No
switch needed (diagnostics); if one is wanted `diag.silent_clock`.

## How to test
Any twoinst test that ends a session with a timeout (coop_rejoin_net.py drops the guest): assert the "last heard"
value in the host's sporekit.log is within 0..timeout_ms+1000: FAIL on main (a huge or wrapped number), PASS with the fix.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
