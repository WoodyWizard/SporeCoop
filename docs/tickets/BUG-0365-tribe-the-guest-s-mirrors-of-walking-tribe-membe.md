---
id: BUG-0365
title: Tribe: the guest's mirrors of walking tribe members stop and go at every start and end of a walk (drive::step; no sender time)
status: open
kind: desync
stage: Tribe
reported: 2026-11-01
source: owner's request after BUG-0360 (re-check the other modes)
role: guest
versions: 0.7.3.3
fixed_in:
branch: fix/w30-tribe-walk
commits:
tests: tribe_walk, tribe_walk_mobile (tools/tests/coop_tribe_walk.py)
signature:
docs: docs/re/tribe.md#tribe-mirror-walk, docs/06-testing.md#testing-tribe-walk
related: BUG-0360, BUG-0281, BUG-0329, BUG-0122
switch: tribe.mirror_walk, tribe.mirror_time
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
