---
id: BUG-0529
title: civ: a city's building hit points are not mirrored: a city the host's vehicles hit stands at full hit points on the guest
status: open
kind: desync
stage: Civ
reported: 2026-11-04
source: lab (fix/w41-civ-combat, coop_civ_shots.py)
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
Found while testing BUG-0477 (coop_civ_shots.py, civ_npc): the host's vehicles attack an NPC city for 60 s; the
sum of the city's building hit points (`coop.civ buildings <city>`) falls 5200 -> about 4450 on the host and
stays 5200 on the guest (lab 20261104-015632). A guest gets only the host's ruins (a destroyed building,
reconcile_buildings), not the hit points of a building that still stands. Players see a damaged city whole
in the guest's game until a building falls. How to reproduce: `tools/lab.py run civ_shots`, the line `city hp`.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Inferred: the MSG_CIV_STATE building rows carry noun, position and a ruined flag, no hit points (civ.cpp host_tick).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
