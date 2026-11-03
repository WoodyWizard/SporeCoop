---
id: BUG-0487
title: Creature: now and then an NPC near the guest has no proxy, or its proxy shows other hp / alive
status: open
kind: desync
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
Detector: `Creature:host_only:creature` 2% of Creature comparisons (up to 9 host NPCs 28 m from the guest without a proxy), `hp:creature` 1% (3 host / 6 guest), `alive:creature` once (dead on the host, alive on the guest), `host_local:creature` once (5 host creatures the mod does not track). Classes G5 (BUG-0328: state for an unknown id dropped, spawn sent once) and G2.
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
