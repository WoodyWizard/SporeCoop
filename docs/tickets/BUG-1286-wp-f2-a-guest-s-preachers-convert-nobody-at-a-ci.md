---
id: BUG-1286
title: WP-F2: a guest's preachers convert nobody at a city the host's camera never saw (fork BUG-9086)
status: open
kind: bug
stage: Civ
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9086
role: guest
versions: 0.7.15.2
fixed_in:
branch: fix/w56-sync
commits: bc78d7e3
tests: civ_preach_far, civ_preach_far_off
signature:
docs: docs/re/civ.md#civ-influence-relay
related: BUG-1285
---

## Report
Found by the Lineages fork (PR #196), its BUG-9086 (row 12): religious vehicles at a city the host's camera never looked
at convert nobody (the city's preaching slots have 0 / 0 places) while the city still damages them. A guest away from
the host (whose orders run in the host's game) cannot take cities by religion. Not reproduced on our lab yet.

## Root cause
Ghidra (GOG 2024): the places are written only by City_SetPreachingCapacity 0xBD9FF0, whose only caller is the camera
city's crowd controller 0xD57B00; the preach tick 0xBEB310 bounds the converts by slot+0 minus slot+8. Our docs had the
preach tick as 0xBEB2F0 with one argument (GOG 2016): CORRECTION added, it is 0xBEB310 (city, slot, activePreachers) ret 8.

## Fix
`coop.fix civ.remote_preaching` (civ_influence.cpp): the host hooks the preach tick and gives a slot with 0 / 0 places
the controller's counts 10 / 2 before the game's tick runs.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
Pending: written and registered, not run yet (the lab waits for the orchestrator's "lab ok" in this batch: the CPU is
busy with another task). Runs to do: `civ_preach_far` (must PASS) and `civ_preach_far_off` (twin, must FAIL).

## History
- 2026-11-19: opened.
- 2026-11-19: fix and tests on fix/w56-sync (switch on); status stays open until the lab runs (FAIL twin, PASS).
