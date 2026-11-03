---
id: BUG-0479
title: Space: the partner's ship and NPC ships are drawn at another scale on the other game (4.82 vs 0.84)
status: open
kind: desync
stage: Space
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
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
Detector: `Space:scale:ufo` in 97% of 126 Space comparisons (e.g. 4.82 host, 0.84 guest; 7.23 / 1.26 earlier); `Creature:scale:ufo` once. A mirror / puppet ship gets another scale than the original (the game scales UFOs with the camera distance or the planet / galaxy view?). To check: what the player sees (screenshot pair), then which scale is right. Also `Space:noun_only_host:planetary_artifact` (9 artifacts in the host's game only) in 4%.
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
