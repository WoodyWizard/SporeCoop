---
id: BUG-1372
title: Tests: civ_preach_far: the NPC city's retaliation ends all three forced preachers before their first conversion (flake)
status: fixed
kind: test
stage: Civ
reported: 2026-11-20
source: lab
role: 
versions: 
fixed_in: unreleased
branch: fix/w61-civ
commits: 5c29c021
tests: civ_preach_far, civ_preach_far_off (twin)
signature: FAIL the preachers converted citizens of the far city on the host (0 -> 0)
docs: docs/re/civ.md#civ-influence-relay
related: BUG-1286, BUG-1322
---

## Report
Second final check of 0.7.16 (lab 20261120-133703, lane 4, warm run 2 after civ_shots and civ_shot_objects):
civ_preach_far `FAIL the preachers converted citizens of the far city on the host (0 -> 0)` and `FAIL the guest's game
shows them (0)`, while `the far city's preaching slots have places` passed (slot 2: 10 / 2 / 0). Passed in the first
final check (20261120-041304) and in the earlier rel/0.7.16 and fix/w57-sync runs (converts within 3-19 s).

## Root cause
Observed (test log and host log of 20261120-133703): the hook set slot 2's places (`preaching slot 2 of city 1120D030
had no places (no camera there): 10 / 2 set (BUG-1286)` at s 24.1, the order at s ~21), so the mod's part worked. The
slot's used count (+8) stayed 0, nobody converted, and all three forced preachers were gone from the host's game by
s 30 (`civ(host): vehicles=4` as before the three spawns; the test's end: `#16/#17/#18: host None`). In every passing
run of this test the slot's used count was 9-10 with one or two of the three preachers still there; the docs already
note "of three forced preachers at a far city the host often had only one left later (the city's retaliation
0xBEB6F8)" (docs/re/civ.md#civ-influence-relay). So the test raced the game's retaliation against the first
conversions; this time the city won before the first conversion. Not a mod fault: no fault, no mismatch, the places
were set. Inferred: the warm run 2 after two other preaching / shooting tests made no difference to the mechanism
(the first final check's pass was a warm run 1 after civ_shot_objects).

## Fix
Test only (coop_civ_influence.py --case preach): the preachers' full health is read after their spawn and put back
every second (`coop.civ vhp`, as civ_capture_credit keeps its besiegers alive) while the test waits for the
conversion; the fewest preachers alive is printed. The checks are unchanged: the places, the host's conversion and the
guest's copy. The twin civ_preach_far_off (civ.remote_preaching off) keeps 0 / 0 places and must still FAIL on the
places and the conversion (the preachers living longer converts nobody there: the game bounds the converts by the
places).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- FAIL (the race, before): 20261120-133703:civ_preach_far.
- Was waiting for the lab run: civ_preach_far must PASS (preachers alive while it waited: 3 of 3), the twin
  civ_preach_far_off must FAIL (slots 0 / 0, nobody converted).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): civ_preach_far PASS in 20261121-003202, 20261122-194210, 20261123-023643; the twin civ_preach_far_off FAIL in 20261121-000724 (the far city's preaching slots empty). Closed.

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-civ): cause from the logs, the test keeps its preachers alive; no lab run (owner rule).
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
