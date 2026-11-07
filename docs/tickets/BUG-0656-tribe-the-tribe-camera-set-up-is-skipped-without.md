---
id: BUG-0656
title: Tribe: the tribe camera set-up is skipped without a simulator camera (tribe_theme_music, tribe_hit_impacts)
status: open
kind: bug
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
5 runs: `game fixes: skipped the tribe camera set-up without a simulator camera` (fix/w44-tribe-guest out/lab/20261105-115653/tribe_theme_music.guest.game.log:110; fix/w45-tribe tribe_hit_impacts). Related: BUG-0167.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the orchestrator's lab log sweep; planned for 0.7.10.
