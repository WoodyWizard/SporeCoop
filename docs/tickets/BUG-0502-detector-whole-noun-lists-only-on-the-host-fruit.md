---
id: BUG-0502
title: detector: whole noun lists only on the host (fruit_group, obstacle, node, nest) in editor_desync
status: open
kind: bug
stage: Creature
reported: 2026-11-03
source: detector report of the regression run out/lab/20261103-150248 (fix/w40-crashes)
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
One detector sample of editor_desync (regression run out/lab/20261103-150248, SPORE_DETECTOR=20, fix/w40-crashes) had whole noun lists in the host's Creature world and none in the guest's: creature 103, citizen 150, herd 133, fruit_group 864, obstacle 512, node 15, nest 103 (+ host_only nest). Seen once (3%); most likely a sample taken while the guest was reloading the host's world (editor_desync switches stages and rejoins), i.e. a detector timing rule rather than a game desync. Not analysed; to check: the guest's stage at that sample, and whether the detector should skip a guest that is loading.
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
