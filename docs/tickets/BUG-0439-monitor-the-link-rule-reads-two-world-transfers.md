---
id: BUG-0439
title: monitor: the link rule reads two world transfers 18 s apart as an unhealthy link on a clean network
status: fixed
kind: test
stage: all
reported: 2026-11-01
source: lab 20261101-173721 busy_gate#3 (fix/w34-vote-cinematic)
role: host
versions: main 279e364 + fix/w34-vote-cinematic
fixed_in: 0.7.6
branch: fix/w42-infra
commits: e64551ba, 37990984
tests: tools/tests/gates_selftest.py two_shares (make test), busy_gate
signature:
docs:
related: BUG-0404, BUG-0405, BUG-0415
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
Already fixed by BUG-0415 (e64551ba, merged 2026-11-01 17:31, minutes before the run of this report on a branch without it): the link rule leaves out the trace lines while the host sends its world to that partner and 15 s after (`share_after`). Observed 2026-11-04: `monitor.py replay` of busy_gate#3 (fix/w34-vote-cinematic out/lab/20261101-173721) with the monitor before e64551ba prints the violation of this report (`resends/sent 0.53; 0.88; 0.63` from s 60.007); with the current monitor the same logs are clean. 37990984: busy_gate no longer waits 35 s between its two shares; gates_selftest plants two transfers 18 s apart.

## Verification
gates_selftest `two_shares`: two shares 18 s apart with resends/sent 0.6, 0.9, 0.7 over three lines: no violation; the same lines with share_after=-1 (no share rule): a link violation (the case bites). Lab: busy_gate without the 35 s wait on lane 4: PASS, no MONITOR-VIOLATION (out/lab/20261104-120727 in fix/w42-infra; shares at s 56.9 and s 73.5). Old monitor FAIL / current PASS: the replay above.

## History
- 2026-11-01: opened.
- 2026-11-04: closed on fix/w42-infra: covered by the BUG-0415 rule since e64551ba (replay shows it); regression case, busy_gate wait removed.
- 2026-11-04: released in 0.7.6.
