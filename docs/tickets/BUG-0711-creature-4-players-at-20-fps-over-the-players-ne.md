---
id: BUG-0711
title: Creature, 4 players at 20 fps over the players' network: a guest draws a partner 16.5 m off its own position after a 1.2 s state gap (monitor drawn)
status: fixed
kind: test
stage: Creature
reported: 2026-11-08
source: fix/w46-k7-findings lab 20261108-012301 multi4_creature_slow
role: guest
versions: 
fixed_in: 0.7.11
branch: fix/w46-k4-bodies
commits: cf460050
tests: tools/tests/gates_selftest.py (f_drawn_stale, f_drawn_stale_off), multi4_creature_slow
signature: MONITOR-VIOLATION drawn guest: guest draws extra1 16.5 m from where extra1's own game has it (limit 5.2 m, Creature) on 2 lines from s 170
docs: docs/06-testing.md#testing-lead-fall
related: BUG-0674, BUG-0675, BUG-0170
---

## Report
multi4_creature_slow (SPORE_FPS=20 SPORE_NET=players, seed 1035603402) on fix/w46-k7-findings a1d1cc7a, lane 10, lab
20261108-012301: the only failure was the monitor gate `drawn guest: guest draws extra1 16.5 m ... (limit 5.2 m)` on
2 lines from s 170. The guest's partner line for Carol at s 170.052: `state_age=1.2 s rtt=142 ms drawn_v=2.6`, flags 0x3
(moving); at s 180 state_age 0.0. Inferred: a latency spike of the players' network (spike_every 60 s) left the
guest's copy 1.2 s without a state: the K4 class (BUG-0674 / BUG-0675, a body extrapolated or held through a spike).
No fault, no freeze. Reproduce: `tools/lab.py run multi4_creature_slow` (seen once).

## Root cause
Observed (the four game logs of lab 20261108-012301 of fix/w46-k7-findings, `monitor.py replay`): the 16.5 m is the
guest's partner line at s 190 (state_age=3.6 s, drawn_v 4.7) against Carol's own line at s 190.04; at s 170 the same pair
was 5.1 m (state_age 1.2 s). The guest's link was dead both ways for ~5.5 s: the host, Carol and Dave all logged "Bob is
not responding" at s 191.4-191.5 and "responding again" at s 193.6, and the guest "Carol is not responding". No copy can be
where its partner is 3.6 s after its last state; not the K4 drawing (the puppet now also slows down past its newest state,
drive.puppet_fall).

## Fix
The test: the monitor's `drawn` invariant leaves out partner lines whose state_age is over drawn_age (1.0 s) + the round
trip (a dead or stalled link is the `link` / `puppet` invariants' and the "not responding" notice's). `SPORE_MONITOR=
'{"drawn_age": -1}'` is the rule before it.

## Verification
- FAIL: the replay of lab 20261108-012301's logs with drawn_age -1: `drawn guest: guest draws extra1 16.5 m ... from s 170`; gates_selftest
  f_drawn_stale_off.
- PASS: the same replay with the fix: no violation; gates_selftest f_drawn_stale (RESULT: OK).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k4-bodies, K4): cause found (a 5.5 s dead link of the guest); the monitor rule fixed (drawn_age), kind test.
- 2026-11-08: released in 0.7.11.
