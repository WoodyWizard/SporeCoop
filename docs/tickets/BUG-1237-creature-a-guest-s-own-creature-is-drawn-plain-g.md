---
id: BUG-1237
title: Creature: a guest's own creature is drawn plain green for about 2 s after a change (model not baked yet?), candidate from key frame S/050
status: open
kind: bug
stage: Creature
reported: 2026-11-18
source: key-frame review of final check 20261117-154911
role: 
versions: unreleased
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Key-frame review of the second 0.7.15 final check (out/frames-0.7.15-b.md, finding 5): S/050 of lab 20261117-154911 shows the guest's own creature plain green 2.2 s after a change; probably the game's bake not done yet. A candidate: not checked whether it lasts or happens without co-op.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened from the key-frame review (candidate); next release.
