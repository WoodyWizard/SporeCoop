---
id: BUG-0602
title: Civ: a city building stands at another place in the guest's game (detector planet:building_industry / house / entertainment, campaign_space)
status: open
kind: desync
stage: Civilization
reported: 2026-11-05
source: detector, campaign_space lab 20261105-132636 (fix/w44-world-state2); rel/0.7.7 final 20261105-015350
role: guest
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
Found by the detector while checking BUG-0593 (fix/w44-world-state2). campaign_space with `SPORE_DETECTOR=20`, lab
20261105-132636: `Civilization:planet:building_industry` in 24 of 65 Civ comparisons ("9 on the host's planet, 9 on
the guest's; 1 only on the host, 1 only on the guest"), the same for building_house / building_entertainment (6);
also in the rel/0.7.7 final check (20261105-015350, building_industry 30 samples, 24 %). The counts are equal: one
building of a city stands at another place (more than 8 m off) in the guest's game. Not the owner (no planet_owner
row after BUG-0593's fix). ~ Inferred: a building the host's city grew or the planner moved went to another slot on
the guest (SnapBuildingToSlot picks the free slot nearest the given position, docs/re/civ.md 1).
Reproduce: `SPORE_FREE_LANE=<free> SPORE_DETECTOR=20 tools/lab.py run campaign_space --lane N`,
`tools/detector_report.py <run>`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-05: opened.
