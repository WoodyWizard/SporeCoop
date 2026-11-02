---
id: BUG-0439
title: monitor: the link rule reads two world transfers 18 s apart as an unhealthy link on a clean network
status: open
kind: test
stage: all
reported: 2026-11-01
source: lab 20261101-173721 busy_gate#3 (fix/w34-vote-cinematic)
role: host
versions: main 279e364 + fix/w34-vote-cinematic
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0404
---

## Report
busy_gate#3 in lab 20261101-173721 (lane 5) met every check of its own and failed on the monitor:
`MONITOR-VIOLATION link host: host -> partner 1: link unhealthy on a clean network on 3 trace lines in a
row from s 60.007: resends/sent 0.53; 0.88; 0.63`. The test sends the host's world twice 18 s apart
(`coop.share due` in share_cinematic and share_paused: 3.3 MB each, "transfer queued completely in 2.6 s").

## Root cause
Observed: the host's trace `resends=` grows by about half of `sent` during a world transfer (the guest saves
nothing back while it leaves its stage and loads), e.g. 12483/20553 -> 20259/32239 between s 60 and s 80.
One transfer covers one or two trace lines (10 s apart); two transfers close together make the three lines
in a row of the link rule (monitor.py `Link`, link_ratio 0.5, link_lines 3). Inferred: any test or session
with world shares less than ~30 s apart can trip it; the rule does not leave out the lines of a transfer.
Not analysed: whether the resends during a transfer are needed (the world's own chunks) or a sign of a
send window that is too big for a loading peer.

## Fix
Not fixed (outside BUG-0404's branch). The test busy_gate now waits 35 s between its two shares. Candidates:
the monitor leaves out the trace lines of a world transfer (the host's own log says when it started and
when it was queued completely), or the trace line carries the transfer's share of the resends.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
