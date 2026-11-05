---
id: BUG-0601
title: Tests: creature_air fails warm right after nest_heal (the NPC never strikes at the guest in the air)
status: open
kind: test
stage: Creature
reported: 2026-11-05
source: rel/0.7.8 lab 20261105-121717, fix/w44-guest-player2 20261105-121010
role: guest
versions: 0.7.8-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
creature_air fails on its precondition "the NPC kept hitting at the guest's creature in the air in the host's
game (0)" whenever it runs warm right after nest_heal: rel/0.7.8 (no P2 code) out/lab/20261105-121717 (also a
sync-check n apart 93.9 m), fix/w44-guest-player2 20261105-121010 and two earlier runs of that branch. It passes cold
(20261105-114931) and in other orders. Likely state nest_heal leaves (the NPCs near the nest, a heal or a herd
state) that the warm reset does not undo. Reproduce: `tools/lab.py run nest_heal,creature_air --lane N`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-05: opened.
