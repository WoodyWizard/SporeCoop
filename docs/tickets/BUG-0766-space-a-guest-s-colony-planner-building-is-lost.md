---
id: BUG-0766
title: Space: a guest's colony planner building is lost when the host's ship is on another planet
status: open
kind: desync
stage: Space
reported: 2026-11-08
source: fix/w46-b1-space (BUG-0751, 2026-11-08)
role: 
versions: 0.7.11 and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0751
---

## Report
The limit of BUG-0751's fix (fix/w46-b1-space, `civ.space_planner_send`): a guest's building bought in a Space
colony's city planner is built in the host's game only when the host's ship is on the same planet (the planet's
cities exist only while it is the active planet). Otherwise the host logs `civ: space planner: <name>'s building ...
not built: this game is at <place>` and counts `space_planner: away=` (coop.civ); the guest keeps its building, the
host's world (and so every later share) does not have it, the money was spent for both.

Reproduce: coop_space_planner_build.py with the host's ship in the system or on another planet before the guest's
drag. Next step: store the building in the planet record's city data (cCityData) on the host, or hold it until the
host lands there.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
