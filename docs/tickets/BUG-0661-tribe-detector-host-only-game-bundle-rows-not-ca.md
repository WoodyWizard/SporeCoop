---
id: BUG-0661
title: Tribe: detector host_only game_bundle rows (not carried bundles) 2-5 rows a run in play_tribe, not covered by BUG-0617
status: open
kind: desync
stage: Tribe
reported: 2026-11-06
source: lab log sweep follow-up 2026-11-06 (detector rows rel/0.7.8 vs rel/0.7.9)
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
play_tribe Tribe:host_only:game_bundle rel/0.7.8 1-2 rows of n2, rel/0.7.9 2-5 rows of n2-3. BUG-0617 covers carried bundles; BUG-0472 notes plain bundles have no kind yet.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the lab log sweep follow-up; planned for 0.7.10.
