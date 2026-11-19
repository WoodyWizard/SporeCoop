---
id: BUG-1296
title: Net: a join can stall on a lossy link, reliable packets have no rate control (fork BUG-9098)
status: not-reproduced
kind: bug
stage: All
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9098
role: 
versions: 0.7.15.2
fixed_in:
branch: fix/w56-general
commits: efbcfbdf
tests: unit (net_tests test_bulk_bottleneck)
signature:
docs:
related:
switch: none (no mod change: a regression test)
---

## Report
Found by the Lineages fork (PR #196), its BUG-9098 (open there): on a lossy internet path a 3.4 MB world crawled (40 KB
in 54 s) while the host also sent live Cell snapshots to a guest still in the galaxy menu. The fork added a snapshot
gate (ours since BUG-0809, `own_world::peer_here`) and `net.pacing` (a token bucket on packets with reliable
messages). Its own test showed no speed gain, only fewer packets dropped at the bottleneck; its ticket stays open.

## Root cause
Not reproduced in ours. net_tests test_bulk_bottleneck models such a path with our Connection and Session::flush's
burst rule: 200 packets/s through a 16-packet drop-tail queue, 60 ms each way, 1 % loss both ways, 1500 messages of
1.1 KB (1.6 MB): all arrive in order in 7.7-8.0 s (the path's best is 7.5 s), the player state arrives 98 %; a worse
path (50 packets/s, an 8-packet queue, 3 % loss, 600 messages): 12.8-13.2 s (best 12 s), player states 94-97 %. Our
sender does overshoot the queue (82-92 % of its packets are dropped there and sent again), which is waste, not a
stall: the transfer runs at the path's rate. The stall the fork saw came with the Cell snapshots on the same path
(BUG-0809).

## Fix
No mod change. The model stays as a regression test (a stall would fail it). Rate control on reliable packets
(the fork's idea) would cut the waste but not the time; not taken now.

## Verification
- Unit: net_tests --only test_bulk_bottleneck PASS (both paths, numbers above).
- no-fail-run: not reproduced in ours (the transfer finishes at the path's rate); the test is a regression check.

## History
- 2026-11-19: opened.
- 2026-11-19: checked on fix/w56-general (WP-F1): not reproduced; found by the Lineages fork (PR #196), its BUG-9098.
