---
id: BUG-0477
title: Civ and Space: shots and projectiles of the host's game are not on the guest's (tool projectiles, cultural, resource, beam, AoE areas)
status: open
kind: desync
stage: Civ
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
Detector: `Civilization:noun_only_host:default_tool_projectile` 20% of Civ comparisons, `resource_projectile`, `cultural_projectile`, `default_ao_e_area`, `Space:noun_only_host:default_beam_projectile`, `host_only` near the guest. The guest sees the hit (damage is relayed) but not the shot. Effects relay (BUG-0451) does not cover projectiles. Class A (sync-architecture-plan).
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
