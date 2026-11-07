---
id: BUG-0652
title: Tribe: the guest's game does not take the held tool of a member (fields: tribe_member_tool applied 6, the game reads 11)
status: open
kind: desync
stage: Tribe
reported: 2026-11-06
source: lab log sweep 2026-11-06 (703 runs, 2026-11-05/06)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Seen in 55 runs on 10 branches, also on rel/0.7.9 runs 20261106-153514 (3 tests) and 20261106-200316 (4 tests): `fields: tribe_member_tool N tool: applied 6.000, the game reads 11.000`. The held-tool replication (tribe.tool_hold) does not take on the guest. Example: out/lab/20261106-013035/tribe_coop.guest.game.log:406 (rel/0.7.9 worktree). Related: BUG-0514 (fixed 0.7.5) names the field but not this warning.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the orchestrator's lab log sweep; planned for 0.7.10.
