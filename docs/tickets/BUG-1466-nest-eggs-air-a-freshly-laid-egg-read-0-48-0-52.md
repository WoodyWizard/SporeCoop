---
id: BUG-1466
title: nest_eggs_air: a freshly laid egg read 0.48-0.52 m over the surface before the grounding placed it (3 runs)
status: open
kind: test
stage: Creature
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in:
branch: 
commits:
tests: tools/tests/coop_nest_eggs_air.py (nest_eggs_air)
signature:
docs:
related: BUG-0384
---

## Report
Validation run of 0.7.16 (lab 20261123-023643, mod code 5376d388): nest_eggs_air "FAIL the eggs the game laid lie on the surface (over 0.3 m: {'1E1FD4E0': 0.52})": `host eggs after 38 s (ptr: over surface): {'1416B1C0': 0.04, '1E1FD4E0': 0.52}`; later `host eggs now (over surface, m apart): [0.0, 0.0]`, `nest eggs fix=1 grounded=2`. The same signature twice before (0.48, 0.49).

## Root cause
Inferred, not proven: the check reads a freshly laid egg before the mod's grounding (nest eggs fix, grounded=2) placed it; whether the game lays it 0.5 m up by itself (and how long it stays there) is not measured. Not a 0.7.16 regression (seen before).

## Fix
None yet. Next: read the egg's height every 0.5 s from its laying (when does it land, by the game or by the grounding), then either wait for the grounding's state in the test or ground at once in the mod.

## Verification
Open.

## History
- 2026-11-23: opened (fix/w65-tests).
