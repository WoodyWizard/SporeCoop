---
id: BUG-0531
title: Tribe: a member stalled in the gather node (0x0569236E, state 1, walk goal an old tree 100 m off) does not act on a new order
status: open
kind: bug
stage: Tribe
reported: 2026-11-04
source: split from BUG-0505 (fix/w41-tribe-orders, 2026-11-04)
role: guest
versions: 
fixed_in:
branch: fix/w42-tribe-left
commits:
tests: tribe_gather_stall (coop_tribe_gather_stall.py)
signature:
docs: docs/re/tribe.md#tribe-gather-orders
related:
---

## Report
Seen in play_tribe (out/lab/20261104-030728 #3 in the fix/w41-tribe-orders worktree): member #29 has the guest's new order record (walk, then hunt the epic, target set) but stays in the gather node 0x0569236E, state 1; its walk goal is still the old tree 100 m off and it does not move. A new order does not take a member out of a stalled gather. Inferred, not shown: host orders suffer the same. Details: docs/re/tribe.md#tribe-orders. Target: 0.7.6 or a 0.7.5.x.

## Root cause
fix/w42-tribe-left (GOG 2024, Ghidra + runtime lane 6, docs/re/tribe.md#tribe-gather-orders):
* Observed (disassembly): the gather node's state 1 (tick 0xD75F70) returns "running" until the member carries a
  bundle (its own cGameBundleContainer c+0x38) or holds a 0x30000 handheld, and then only leaves on the walk's
  arrival (0xC42F80): no time limit. The exit 0xD75520 does not touch the order queue.
* Observed (runtime, 7 runs of the new tribe_gather_stall test): a guest's walk order given while its member is in
  the gather node (state 1 walking to a tree 90 m off, or state 19 carrying the food home) took the member out of
  the node every time (node 0x04626360 within 3 s, at the goal on both games).
* Inferred, not shown: the play_tribe stall needs state 1 with the walk itself stopped short of the tree (standing,
  walk mode 1), the same kind of stop as the grab spots of BUG-0505. Not reproduced, so not fixed.

## Fix
None (not reproduced). A watchdog that ended the gather node (state 0xF) / re-gave a lost order was written and
removed again: without a failing run it could not be shown to help. Diagnostics added: `coop.tribe ai` prints the
food carried (`carry=`) and the held tool; `coop.tribe order <id> <action> <x y z>` (host) gives a member an order
as host_command does.

## Verification
no-fail-run: not reproducible with tribe_gather_stall (coop_tribe_gather_stall.py), which passed its order checks in
out/lab/20261104-100046 (2/2), 20261104-100604 (3 runs: 2 OK, #3 failed only its control walk, the walk ending at
the hut's edge), 20261104-100245 (order checks: 1 of 2 runs had the walk stop 8.8 m short at an obstacle). The test
stays as a regression check (suite tribe_gather_stall).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-tribe-left): gather node RE, not reproduced in 7 runs, no fix; regression test tribe_gather_stall.
