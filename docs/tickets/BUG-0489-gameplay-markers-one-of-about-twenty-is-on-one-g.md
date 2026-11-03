---
id: BUG-0489
title: gameplay markers: one of about twenty is on one game only or elsewhere (Creature 14%, Tribe 2%): find what a gameplay marker is before deciding
status: open
kind: test
stage: Creature
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, out/detector/20261103-final.md)
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
Detector: `Creature:unmatched:gameplay_marker` in 14% of Creature comparisons (21 host / 20 guest, 1 without a partner within 3 m), `Tribe:unmatched:gameplay_marker` 2%. kGameplayMarker (0x036BE27E) is not described in docs/re: find what places it (nests, missions, the player's own goals) in Ghidra; if per player, add it to worlddiff.IGNORE with that reason, else a sync ticket.
Found by the lab (no player report). Target: 0.7.4.
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
