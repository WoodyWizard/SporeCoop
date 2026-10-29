---
id: BUG-0227
title: an armada wave resumed after the host's return can have normal-HP ships, be won without a fight, or ignore a raid lost meanwhile
status: fixed
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #161 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-space
commits: ecbecc3, 5138792, f3e0432, 344f82f
tests: space_armada_return (coop_space_armada_return.py, BUG-0227 parts); unit: tests/space_tests.cpp test_wave_return
signature:
docs: docs/re/space.md#space-object-serial
related: BUG-0046, BUG-0218, BUG-0136
switch: contracts.wave_tough, contracts.wave_resume, contracts.wave_away
---

## Report
From GitHub issue #161: https://github.com/WoodyWizard/SporeCoop/issues/161 (the full analysis, links and the scenario are there).

The BUG-0046 fix (9f9fd90, release 0.7.3.0) lets an armada contract's wave resume its own raid when the host comes back to the colony. The resume path has three gaps: the "HP raised once" set holds addresses of ships the game freed, a resumed wave keeps the "its ships appeared" flag from before, and a raid lost while the host was away is not counted (that last gap already existed before the fix). The test only counts raid missions, so it cannot see any of them. Severity LOW (contract balance, no crash).

## Root cause
Observed with the fixes off (lab 20261028-231426): (1) with an address-reuse simulation the resumed wave's ships kept their base max HP (250 / 50 instead of 500 / 100): the raised-once set was keyed by pointer; (2) a resumed wave whose ships did not come was won 30 s later (wave 1 -> 2), because resume kept `g_wave_ships` from before. (3) A raid result while the host is away was dropped (by reading; the listener only took results while `g_wave_on`).

## Fix
(1) the set is keyed by `spacelogic::tough_key` = the object serial (`game::object_serial`), tagged apart from pointer keys; (2) resume clears `g_wave_ships` and marks the wave resumed; when no ship comes in 30 s the old raid, if still in `raids_on_target()` this tick, is ended with `cMission::SetState(7)` and a new raid starts; spacecoop's raid hook sends nothing and pays no bonus for 7 and drops it from `g_raid_since`; (3) the listener takes the wave mission's 5 / 6 while the host is away into `g_wave_away_result`; on return `spacelogic::wave_on_return` fails the contract (plundered) or counts the wave won. Switches `contracts.wave_tough`, `contracts.wave_resume`, `contracts.wave_away`. Test hooks `contracts tough_reuse|hideships 0|1`, `contracts wave`.

## Verification
PASS out/lab/20261028-230613 and 20261028-231750 (resumed ships 500 / 100 like wave 1; with hidden ships the wave stays 1 and one raid is on the colony); FAIL with `SPORE_FIX_OFF=contracts.wave_resume,contracts.wave_tough` out/lab/20261028-231426. Unit test of the return decision and the key in `make test`. Not seen at runtime: the SetState(7) abort itself (in all runs the game had ended the old raid by itself before the 30 s, so the new raid came without an abort) and part (3) (a raid ending while the host is away; unit test only). A lane run on whether raid ships survive the planet-to-system switch was not made: the serial key makes it irrelevant.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-space: serial key, resumed waves wait for ships, away results count; test space_armada_return extended.
