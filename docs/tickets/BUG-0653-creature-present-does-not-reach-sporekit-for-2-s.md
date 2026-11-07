---
id: BUG-0653
title: Creature: Present does not reach SporeKit for 2 s after a 2.5 s hitch at the game saved box confirmation
status: open
kind: bug
stage: Creature
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
32 runs on 16 branches, tests chat_pause, team, select, crash_fruit_eat, frames_loop, body_trail_civ, cell_gone_play (not the hooksplit/freeze/pause tests that cause it on purpose): `Present has not reached SporeKit for 2 s (another overlay took it over?)` right after a 2.5 s Creature hitch while the game saved box is confirmed. Example: rel/0.7.8 worktree out/lab/20261105-111032/team.host.game.log:135. Related: BUG-0347.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the orchestrator's lab log sweep; planned for 0.7.10.
