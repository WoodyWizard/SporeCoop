---
id: BUG-0405
title: world transfer to a guest that loads: 50-90 % of the packets are resent on a clean link
status: fixed
kind: bug
stage: All
reported: 2026-11-01
source: rejoin_race_long, lab 20261101-134020 (feat/w33-tests-session)
role: host
versions: main a05d1f6
fixed_in: 0.7.5
branch: fix/w41-infra
commits: deae3502
tests: rejoin_race_long, rejoin_mobile, unit (net_tests bulk_busy_receiver)
signature:
docs: docs/06-testing.md#testing-g1-infra
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
Observed (code, mods/coop/net/connection.cpp + session.cpp, and the new per-transfer log line): two causes, both on
a clean link.
1. The ack bits of a packet cover the 128 packets before its ack. The host sends up to 128 packets per frame and
   keeps up to 600 messages in flight on a LAN (world.cpp); a guest reads its socket once per frame (256 packets
   per update) and acked once at the end of its frame. Every packet more than 128 before the last one read was
   never acknowledged and was sent again (and again), although it had arrived.
2. The resend timer (rto = 1.5 rtt + 20 ms, at least 100 ms) fired while a loading guest (frames of 200-950 ms)
   had not read its socket yet: everything sent during the stall went out a second time.
Measured (rejoin_race_long, 40 rejoins with a guest that loads while the world comes, clean loopback, switch off,
out/lab/20261104-032411): 27 transfers acknowledged, resends 160 228 of 255 858 packets (63 %, 48-70 % per transfer),
median 2.2 s / 1571 KB/s for 3.4 MB.
Earlier inference (kept): the guest acknowledges on its frame (a stalled frame delays the acks by up to a
second), the host's resend timer follows the rtt estimate (avg 219 ms, max 1407 ms), so the reliable chunks
of the world are sent again while their acks wait in a stalled guest. On a real link with a bandwidth cap
this doubles the transfer's traffic in the moment it matters most (the join). Not a desync: the transfer
completed every time (the monitor's link check now leaves world transfers out, tools/tests/monitor.py
`Timeline.sharing`).

## Fix
`net.bulk_acks` (net::g_fix, coop.fix net.bulk_acks off = the old transport):
* the receiver acknowledges every 48 packets it reads (Session::update sends a packet as soon as
  `Connection::ack_urgent()`), so no packet falls out of the ack bits;
* the sender resends a reliable message only when a packet sent after it was acknowledged (it was lost) or the
  peer has been silent for max(4 rto, 1 s) since it was sent; the rto still comes first.
* world.cpp: `world: transfer to N acknowledged (KB in s, KB/s; packets P, resends R = x %, rtt)` when the guest
  has acknowledged all of a transfer (the statistics for this ticket).
Both sides need the fix for the first part; with an old guest the second part alone changes nothing (the acks
still say the packets are missing). No wire change.

## Verification
- FAIL (the measurement: 63 % resends with the switch off, clean link): out/lab/20261104-032411
- PASS (0 resends, the transfer twice as fast, switch on): out/lab/20261104-033012

Measurements (the tests pass either way: the transfer completed before too; the numbers are the check):
| run | link | switch | transfers | resends / packets | median time | median rate |
|---|---|---|---|---|---|---|
| out/lab/20261104-032411 rejoin_race_long | clean | off | 27 | 160 228 / 255 858 = 63 % | 2.2 s | 1571 KB/s |
| out/lab/20261104-033012 rejoin_race_long | clean | on | 33 | 0 / 117 813 = 0 % | 1.2 s | 2939 KB/s |
| out/lab/20261104-033840 rejoin_mobile | SPORE_NET=mobile | off | 5 | 1 222 / 20 574 = 6 % | 7.5 s | 457 KB/s |
| out/lab/20261104-034043 rejoin_mobile | SPORE_NET=mobile | on | 5 | 1 095 / 20 549 = 5 % | 7.8 s | 441 KB/s |
(mobile: the test's own "world transfer" times 6.7-7.7 s off, 6.6-6.8 s on: the same within noise.)
Unit test net_tests `bulk_busy_receiver` (make test) runs each case with the switch off and on and checks the on side; off, a clean link to a loading guest
(the simulated transfer collapses: 100 % resends), PASSES on: 0 resends, 40 ms / 3 %: 59 % -> 3 %, 150 ms / 10 %:
50 % -> 10 %, never slower.

## History
- 2026-11-01: opened.
- 2026-11-04: fixed on fix/w41-infra (net.bulk_acks), measured before / after.
- 2026-11-04: released in 0.7.5.
