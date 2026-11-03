---
id: BUG-0480
title: Cell: the guest keeps mirrors of cells the host no longer has (stale mirrors) and misses some the host has near it
status: open
kind: desync
stage: Cell
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
Detector: `Cell:guest_only:cell` in 16% of 242 Cell comparisons (a proxy with a host index the host has no cell for near the guest), `Cell:host_only:cell` 2% (up to 75 host cells 25 m from the guest without a proxy), `Cell:scale:cell` 1% (0.93 / 0.78). Class G5 (lifecycle and resync of unknown / gone ids).
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
