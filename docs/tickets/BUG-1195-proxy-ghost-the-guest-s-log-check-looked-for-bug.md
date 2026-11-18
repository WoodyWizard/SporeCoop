---
id: BUG-1195
title: proxy_ghost: the guest's log check looked for BUG-0931's line only; BUG-0842's identity check (0.7.13) names the ghost proxy first, and its twin switch alone no longer fails
status: fixed
kind: test
stage: Creature
reported: 2026-11-16
source: lab
role: 
versions: 
fixed_in: 0.7.15
branch: fix/w51-t5
commits: fa2352a0
tests: proxy_ghost
signature:
docs: docs/06-testing.md#testing-final-0715-t5
related:
---

## Report
Final check of 0.7.15 (lab 20261116-025154, lane 3): `proxy_ghost` FAIL `the guest's log names every proxy whose
creature was gone (0 of 3)`; the other checks passed (every ghost proxy made again, no caught fault). The test last
passed on rel/0.7.12 (lab 20261111-*); it was not run on 0.7.13 / 0.7.14 in the recorded lab runs.

## Root cause
Observed in the guest's log: each of the 3 ghost proxies was named by BUG-0842's identity check (`npc N: the proxy's
creature 03300000 is not the one it made any more ... made again (npc.proxy_identity)`, 0.7.13), which runs in every
frame before BUG-0931's list check (`... is no creature of this world any more`), so the list check never saw them.
The order is the same on v0.7.14.1 (npc.cpp guest_tick: check_identity, the stale drop, check_listed): an old failure
of the test since 0.7.13, not of the mod; the guest drives no ghost proxy either way. For the same reason the twin
`SPORE_FIX_OFF=npc.proxy_listed_now` alone no longer fails: the identity check covers it.

## Fix
tools/tests/coop_proxy_ghost.py: counts per ghost proxy id a line of either check; the twin is
`SPORE_FIX_OFF=npc.proxy_identity,npc.proxy_listed_now` (suite.py comment, the docstring). No mod change.
switch: none (test only).

## Verification
FAIL: final check 20261116-025154 proxy_ghost (rel/0.7.15) and v0.7.14.1 (detached worktree fix/w51-t5-v07141, lab 20261116-084839 proxy_ghost: the same `0 of 3`): an old failure since 0.7.13.
PASS: lab 20261116-085451 proxy_ghost#1, #2 (3 of 3 named, made again, no fault); also 20261116-082817 proxy_ghost#1, #2. Twin: lab 20261116-091833 proxy_ghost with SPORE_FIX_OFF=npc.proxy_identity,npc.proxy_listed_now FAILS (72 caught faults).

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t5 (final-check triage of 0.7.15, group T5).
- 2026-11-18: released in 0.7.15.
