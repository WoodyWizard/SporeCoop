---
id: BUG-0654
title: A game cinematic is still playing in a stage or at a load and is ended by force (about 3000 lines, most runs)
status: open
kind: bug
stage: All
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
About 3000 lines `world: cinematic NNN still playing in a stage / at the load: ended it` in nearly every run on every branch (29 in rel/0.7.9 out/lab/20261106-200316/creature_carry.guest.game.log). Probably a handled recovery; find which cinematics they are and whether ending them by force hides or causes a player-visible problem. Related: BUG-0607, BUG-0626.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the orchestrator's lab log sweep; planned for 0.7.10.
