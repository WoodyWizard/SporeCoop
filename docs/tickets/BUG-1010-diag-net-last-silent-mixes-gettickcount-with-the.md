---
id: BUG-1010
title: diag: net last_silent mixes GetTickCount with the steady_clock connection time, the leave lines print a garbage last heard value
status: fixed
kind: tooling
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-6
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-ui
commits: 8bc5ebf9, 66f43dd7
tests: make test (no lab test: no-fail-run)
signature:
docs:
related:
switch: diag.silent_clock
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
diag_stats.cpp net_second: `nowms = net::now_ms()` (the clock of Connection::last_recv_ms, the Session's own when it
has no test seam) instead of GetTickCount. Switch `coop.fix diag.silent_clock off` (GetTickCount, 0.7.13).

## Verification
* no-fail-run: not reproducible under Wine: GetTickCount and libc++'s steady_clock (QueryPerformanceCounter) both
  count from the same boot clock there, so the difference is a few ms. Seen in the main checkout's lab logs: every
  "last heard N ms" of out/lab/20261103-012624 is 14..877 ms (plausible values). On Windows QPC and the tick count
  are separate clocks with their own drift and resolution; the fix takes both values from one clock (code review).

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
- 2026-11-13: fixed on fix/w49-ui (0.7.14 P2).
