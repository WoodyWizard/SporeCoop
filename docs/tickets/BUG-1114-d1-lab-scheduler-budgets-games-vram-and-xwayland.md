---
id: BUG-1114
title: D1: lab scheduler budgets games, VRAM and Xwayland; fps-sensitive thresholds between measured PASS and FAIL ranges
status: fixed
kind: test
stage: Tools
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-labstart
commits: ee9d9990 ff5df396 53592042 7fdc676c fb1059a8 6c35515c 06a43616
tests: labstart_selftest.py (budget, fpslimit); npc_walk_tribe
signature:
docs: docs/06-testing.md#testing-lab-budget
related: BUG-0663 BUG-1098 BUG-1113
---

## Report
Plan 0.7.15 D1 (TST-3): the lab capped lanes, not games (the 0.7.14 final check ran 22 games where ~18 fit the card,
BUG-1098); npc_walk_tribe failed that final check at p50 0.11 m against a fixed 0.10 at 22 guest fps.

## Root cause
The scheduler counted only its own lanes and RAM; fps-sensitive limits were fixed numbers set at full speed.

## Fix
tools/labbudget.py, a gate after the memory gate: machine-wide SporeApp.exe count (18), the card's VRAM (+0.66 GB a game
to come, 0.5 GB free), at most 10 games and under 80 % Xwayland per display, 12 games while a load-sensitive test
(suite.load_sensitive) runs; a test's games from suite.games (campaign 4); games of tests just started count as
starting; a lane with nothing left ends its games; SPORE_LAB_BUDGET=0 switches it off. npc_walk_tribe's drawn error p50
limit: fpslimit.scaled(fps, 0.10, 3.0, 0.20), i.e. 3.0 / fps within 0.10-0.20 m (SPORE_NPCWALK_FIXED_P50=1: the
fixed 0.10). The other fps-sensitive candidates were measured and left (docs/06-testing.md#testing-lab-budget).

## Verification
FAIL (twin, SPORE_LAB_FIXOFF="npc_walk_tribe=npc.walk_tribe", SPORE_FPS=guest:15): lab 20261115-021549, p50 0.25 and
0.30 m > 0.20 at 15 fps. PASS (fix on, SPORE_FPS=guest:15): lab 20261115-021524, p50 0.16 and 0.16 m <= 0.20 (both
runs OK; the fixed 0.10 would have failed them). History: fix on 0.02-0.04 m at 46-69 fps ... 0.11-0.17 at 16-22
(failed rel/0.7.14 20261114-005006, -050940 on the old limit), twin 0.27-0.29 at 35-56 fps (20261101-085037). The
budget held a real test: lab 20261115-012729 team#3 waited 5 s for its display (10 games there). Unit:
labstart_selftest (decide, Gate, fpslimit).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-labstart.
- 2026-11-18: released in 0.7.15.
