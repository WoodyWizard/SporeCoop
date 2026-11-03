---
id: BUG-0485
title: spice / commodity nodes are drawn at another scale on the guest (10.0 host, 1.0 guest)
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
Detector: `Creature:scale:node` 2% of Creature comparisons, `Tribe:scale:node` 2%, `Creature:scale:node` 10.00 host / 1.00 guest. A commodity node (spice geyser) the host's game scaled is at the default scale on the guest. Check what the player sees (screenshot pair) before fixing. Class G2.
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
- 2026-11-03: investigated on fix/w40-scale: on the creature / tribe fixtures all 16 nodes are 1.0 on both games (the 10 is set at runtime after the live Cell -> Creature start, not saved); the node loco vtable 0x1469A28 uses cModelObject_SetScale, hooked now, which logs `scale 10 set on an object with vtable ... from ...` once per caller: the next campaign_short run names the setter. Not fixed (what the player sees and who sets it unknown); docs/known-issues.md#issue-node-scale.
