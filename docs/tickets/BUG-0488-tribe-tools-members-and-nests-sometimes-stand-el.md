---
id: BUG-0488
title: Tribe: tools, members and nests sometimes stand elsewhere on the guest (more than 15 m / 3 m)
status: open
kind: desync
stage: Tribe
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
Detector: `Tribe:unmatched:tribe_tool` 4% of Tribe comparisons, `unmatched:citizen` 1%, `unmatched:nest` 1%, `host_only:interactive_ornament` once (4 near the guest on the host only). Related to BUG-0481 (play_tribe: members not where the host has them). Class G8 (remote placement).
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
