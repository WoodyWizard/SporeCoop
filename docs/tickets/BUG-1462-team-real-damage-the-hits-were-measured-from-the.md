---
id: BUG-1462
title: team_real_damage: the hits were measured from the 60 % set before the first try; an NPC that did not fight back let the victim regenerate (8.2 -> 6.2 failed '< 6.0 - 0.3')
status: fixed
kind: test
stage: Creature
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w65-tests
commits: def13b5f
tests: tools/tests/coop_team_real_damage.py (team_real_damage)
signature:
docs:
related: BUG-0373
---

## Report
Validation run of 0.7.16 (lab 20261123-023643, mod code 5376d388): team_real_damage "FAIL host: its own game took the NPC's hits (6.2 < 6.00 - 0.3)".

## Root cause
Test: the victim is set to 60 % (6.0) and the check compares with that. The first NPC tried (#18) "did not fight back in 10 s"; meanwhile the host regenerated and #13 bit from 8.20 ("#13 fights back: host 8.20 -> 7.70"); the hits took it to 6.2 (2 hp landed) and the check against 6.0 failed. Passing runs: the first NPC fought back at once (from 6.20, 6.68). Not the mod.

## Fix
coop_team_real_damage.py: provoke() returns the victim's health just before the first bite of the NPC that fought back; the hits are measured from it (the same 0.3 margin).

## Verification
FAIL before the fix: 20261123-023643 (team_real_damage, mod code 5376d388, the test of before def13b5f). PASS with it:
20261123-104630 (team_real_damage, rel/0.7.16.1 30be883d, which holds def13b5f). no-fail-run: a test fix (the hits measured from the bite of the NPC that fought back); def13b5f
is in v0.7.16 (released with it).

## History
- 2026-11-23: opened (fix/w65-tests).
- 2026-11-23: fix/w84-tests (WP-T1 of 0.7.17): closed on the lab evidence: PASS 20261123-104630 on rel/0.7.16.1; fixed_in 0.7.16 (def13b5f is in the v0.7.16 tag).
