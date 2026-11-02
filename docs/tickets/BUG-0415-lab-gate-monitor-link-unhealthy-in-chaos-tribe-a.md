---
id: BUG-0415
title: Lab gate: monitor link unhealthy in chaos_tribe around a guest rejoin (churn) on a clean network
status: fixed
kind: test
stage: Tribe
reported: 2026-11-01
source: lab 20261101-150122 chaos_tribe#3, lane 7 (fix/w33-proxy-ground)
role: host
versions: unreleased
fixed_in: 0.7.3.4
branch: fix/w35-monitor-gates
commits: e64551b, f19d618
tests: monitor_tribe, chaos_tribe, make test (gates_selftest.py)
signature:
docs: docs/06-testing.md#testing-monitor-w35
related: BUG-0416
---

## Report
out/lab/20261101-150122-fix-w33-proxy-ground chaos_tribe#3 (lane 7, `--cold`, SPORE_FIX_OFF=npc.proxy_ground,
unrelated to it) FAILED on the monitor gate `link: host -> partner 1: link unhealthy on a clean network on 3 trace
lines in a row from s 50.013: resends/sent 0.66; pending 401; resends/sent 0.99`. The chaos bot rejoined the guest
4 times in 120 s (`churn=4`); the host's partner line at s 50.0 says resends=7306/11033 (the guest disconnected at
s 52.1 and joined again at s 54.7, two 1 s "saving" hitches of the host at s 40.7 and 56.7), at s 60.0 pending=401
and state_age 0.7 s.

## Root cause
Decided (fix/w35-monitor-gates): the rule, not a link fault a player sees. The host's partner line counts the
packets of the connection; the three bad lines (s 50, 60, 70) each cover a world transfer to the rejoining guest
(host: `world: sharing ... with player 1` at s 41.3 and 57.2, `transfer to 1 queued completely` 1.2 / 2.1 s
later; the guest received 3.5 MB in 1.2 / 2.1 s, loaded and was back in the world at s 47.9 and 64.9). A LAN
transfer keeps 600 reliable chunks in flight (world.cpp, rtt < 40 ms), more than the 128-packet ack window, and
the guest acks late while it receives and loads: 7891 of 7977 packets between s 60 and s 70 were resends, the
host's net line for that connection `resends 13004 ... out 17181 KB` for one 3442 KB world, `loss 0.0%`. The
transfers completed, the guest was in the world and sync-checked OK (s 66.7). Outside the transfers the same
connection had no resends (13004/17200 at s 80, 13004/17625 at s 90). The traffic itself is BUG-0405
(feat/w33-tests-session: 50-90 % resends to a loading guest), a cost on a capped link, not this ticket.

## Fix
Rule only (tools/tests/monitor.py, no mod change): a partner line is not judged by `link` while its machine sends
its world to that partner (`world: sharing ... with player N` to `transfer to N queued completely` / the player
left) and `share_after` (15 s) after it; the run of bad lines restarts after it (`Timeline.sharing`, the same code
feat/w33-tests-session added for BUG-0405). `share_after < 0` (SPORE_MONITOR) is the rule before. A storm outside
a transfer, one that goes on after it, or a transfer to another player still fires (gates_selftest.py
`resend_after`, `resend_other`).

## Verification
* FAIL (the rule before): `monitor.py replay` of out/lab/20261101-150122 (fix/w33-proxy-ground worktree) with the
  old monitor: `chaos_tribe#3: link host: ... resends/sent 0.66; pending 401; resends/sent 0.99`; also
  SPORE_MONITOR='{"drawn_still": -1, "share_after": -1}' monitor_tribe out/lab/20261101-173952 (it failed on the
  drawn part; its two rejoins did not make three bad lines: the planted storm is offline, below).
* PASS: the same replay with the new rule: no link violation. monitor_tribe out/lab/20261101-174232 (two rejoins in
  a row, no link violation). chaos_tribe 3/3 out/lab/20261101-174833 (6 rejoins), smoke out/lab/20261101-180732 (12 game tests OK; unit failed on this branch's ticket index, then in progress; ui skipped: exclusive lanes 1-5).
* Planted: gates_selftest.py (make test) `resend_share` quiet, `resend_after` and `resend_other` fire, `resend`
  (no transfer) fires as before.

## History
- 2026-11-01 (fix/w35-monitor-gates): decided: the rule; link leaves world transfers out (+15 s).
- 2026-11-01: opened.
- 2026-11-02: released in 0.7.3.4.
