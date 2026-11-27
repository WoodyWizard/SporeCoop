---
id: BUG-1594
title: Civ Rivals: Rivals race progress omits player wealth and city win target in PlayerProgress
status: fixed
kind: bug
stage: Civ
reported: 2026-11-26
source: AI playtest (v0.8.2.3)
role: host
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 20fa22f2
tests: rivals_civ_spec_sw, make test
signature: Civ Rivals progress row shows missing money or win target count
switch: none (telemetry field population in civ_rivals.cpp)
docs:
related:
---

## Report
In Civilization Rivals, `PlayerProgress` rows omitted the nation's Sporebucks wealth and non-capital city win target.

## Root cause
`civ_rivals.cpp` did not populate `pr.secondary` (`cc.wealth[i]`) and `pr.extra` (`(float)need`) per the `rivals_logic.h` `PlayerProgress` contract.

## Fix
Populated `pr.secondary = cc.wealth[i]` and `pr.extra = (float)need` in `mods/coop/civ_rivals.cpp`.

## Verification
- PASS run: `out/lab/20261127-001053` (`rivals_civ_spec_sw`, PASS) and `make test`.
- no-fail-run: progress-row telemetry population verified by unit tests and `rivals_civ_spec_sw`.
