---
id: BUG-0580
title: Tribe: the NPC tribes' food differs between the host and the guest (detector Tribe:state:tribe_food)
status: open
kind: desync
stage: Tribe
reported: 2026-11-04
source: detector D0 (lab 20261104-203017)
role: guest
versions: 0.7.6
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0572, BUG-0483, BUG-0421
---

## Report
Found by the detector's world-state rows (D0, docs/06-testing.md#testing-detector-state), no player report: an NPC
tribe's food (its cGameBundleContainer +0x20C) is not the same on the two games. long_tribe on rel/0.7.7 (0.7.6 code),
lab 20261104-203017 and play_tribe of the same run: `Tribe:state:tribe_food` in 51 % of the Tribe comparisons, e.g. "3 of 4
tribes differ in food (the tribe at (446 -150 185): 145 on the host, 0 on the guest)"; a quiet session (detect_tribe, lab
20261104-201322) had 70 / 80 (within the tolerance). The player tribe's food (shared) was the same.
Reproduce: `SPORE_DETECTOR=20 tools/lab.py run play_tribe --lane N`, then `tools/detector_report.py <run>`.
~ Inferred: each game's own NPC tribes gather and spend (the host's are not mirrored as food), the same class as BUG-0572
(food not shared / applied) for the NPC tribes; what a player sees of it (raids, gifts, the NPC tribe's growth) is not known.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
