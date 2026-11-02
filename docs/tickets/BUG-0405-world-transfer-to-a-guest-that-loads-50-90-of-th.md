---
id: BUG-0405
title: world transfer to a guest that loads: 50-90 % of the packets are resent on a clean link
status: open
kind: bug
stage: All
reported: 2026-11-01
source: rejoin_race_long, lab 20261101-134020 (feat/w33-tests-session)
role: host
versions: main a05d1f6
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
`rejoin_race_long` (coop_rejoin_loop.py --rounds 40 --race 1:9, Tribe fixture, lane 9, clean loopback network,
lab 20261101-134020): the invariant monitor's `link` check fired twice on the host, both during a world share to
the rejoining guest: `partner 1 ... resends=4673/6838 ... pending=602` (s 353, share started s 350.4, "transfer
to 1 queued completely (3432 KB in 10.6 s, 324 KB/s, rtt 81 ms)"), and again at s 463 (resends/sent 0.51 and
0.92 between trace lines). The host's per-minute net line: `rtt 18/219/1407 ms ... resends 1323, backlog 160
(max 310)`. The same transfer without a load on the guest side ran at 2603 KB/s (chaos_civ_long guest log).
The guest's frames stall while it loads (fps avg 5-9, hitches 200-950 ms in the same minutes).

How to reproduce: `SPORE_LANE=N python3 tools/tests/coop_rejoin_loop.py --rounds 40 --race 1:9`, then
`grep 'partner 1' sporekit.<host>.log | grep resends` around each `world: sharing`.

## Root cause
Not analysed. Inferred: the guest acknowledges on its frame (a stalled frame delays the acks by up to a
second), the host's resend timer follows the rtt estimate (avg 219 ms, max 1407 ms), so the reliable chunks
of the world are sent again while their acks wait in a stalled guest. On a real link with a bandwidth cap
this doubles the transfer's traffic in the moment it matters most (the join). Not a desync: the transfer
completed every time (the monitor's link check now leaves world transfers out, tools/tests/monitor.py
`Timeline.sharing`).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
