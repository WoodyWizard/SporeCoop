---
id: BUG-0482
title: play_creature: the chat line typed while the creature lives loses a line now and then (1 of N), with and without the effect relay
status: open
kind: test
stage: Creature
reported: 2026-11-03
source: nightly detector run 2026-11-03
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
play_creature failed with `the chat line takes the typing while the creature lives (1 lines lost; while dying: 0)` in out/lab/20261103-012624 (2 of 3 runs, main 0.7.3.6) and in out/lab/20261102-232023 (rel/0.7.3.6b, no relay): the typing of one chat line does not reach the chat while the bot plays. Input focus / the chat box opening race (G13 input), or the bot types before the box is open.
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
