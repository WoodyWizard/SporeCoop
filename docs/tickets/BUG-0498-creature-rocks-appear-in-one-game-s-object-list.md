---
id: BUG-0498
title: Creature: rocks appear in one game's object list only, dozens at once 100-160 m from the walking player (detector unmatched:rock)
status: open
kind: desync
stage: Creature
reported: 2026-11-03
source: divergence detector, fix/w40-world-objects lane 9 out/lab/20261103-131403
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
Detector `Creature:unmatched:rock` in 62% of the comparisons of out/lab/20261103-131403 (1 rock on the host, 6-7 on the guest near it), none in the three runs before. Live (lane 9): the guest's list had 1 rock (serial 2554, as the host), 29 after its avatar walked 20-30 m (serials 5323.., 100-160 m from it), 1 again 40 s later; the host's stayed 1. Inferred: rocks made by each game near its own camera (like hanging fruit), so a detector artefact; not checked in Ghidra. Not caused by worldobj.cpp (no rock kind). Target: 0.7.4.
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
