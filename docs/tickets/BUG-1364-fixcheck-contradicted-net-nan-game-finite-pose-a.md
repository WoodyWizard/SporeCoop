---
id: BUG-1364
title: fixcheck CONTRADICTED: net_nan / game.finite_pose and npc_nan_vel / game.finite_vel claim a FAIL their twins never showed
status: fixed
kind: test
stage: Creature
reported: 2026-11-20
source: final check 20261120-133703 (5-fixcheck)
role: 
versions: unreleased
fixed_in: unreleased
branch: fix/w61-creature-d
commits: de235059 c67ac9af
tests: net_nan, npc_nan_vel
signature:
docs: docs/06-testing.md#testing-fixcheck-nan-claims
related: BUG-0614, BUG-0612, BUG-0894
---

## Report
0.7.16 second final check (out/night-0.7.16-b/5-fixcheck.txt, `lab.py fixcheck`): two of the four CONTRADICTED claims
are in this area: net_nan claims "SPORE_FIX_OFF=game.finite_pose fails" (runs with the switch off: 0 FAIL, 1 PASS,
20261106-043542) and npc_nan_vel claims "SPORE_FIX_OFF=game.finite_vel fails" (0 FAIL, 2 PASS, 20261112-073101,
20261112-074517). The release rule (docs/06-testing.md#testing-release-rule, 3) allows none.

## Root cause
The claims were written before the twins ran and were never true. Both tickets already record it: BUG-0614 is
`no-fail-run` (the scripted guest's NaN / Inf states did not reach its drawn puppet with the guard off: 0 of 980 / 990
`coop.bodyrec` rows not finite, the game's puppet driving and spatial SetPosition tolerate them), BUG-0894 is
`not-reproduced` (6 s of NaN velocities in 1155 NPC states, on the ground and in the air: all 21 proxies finite with the
guard off; the drivers place each proxy from the finite position every frame, a NaN walk target is refused by
pose_refused). No other path that reaches a body is known: a test that "makes it fail" would have to plant the NaN past
the guard, which proves nothing about the game. The guards stay as defences; the tests stay as regression tests of
them.

## Fix
Tests and docs only, as leave_avatar's contradicted claim was handled (docs/06-testing.md, `lab.py fixcheck`): the
suite.py comments of net_nan and npc_nan_vel and the two tests' docstrings no longer claim a FAIL with the switch off;
they name the PASS runs with the switch off and the tickets' no-fail-run. The switches game.finite_pose and
game.finite_vel and the guards are unchanged.

## Verification
no-fail-run: claim correction only (the FAIL was never reproducible: BUG-0614, BUG-0894). Check without a game:
`python3 tools/lab.py fixcheck` no longer lists net_nan / game.finite_pose and npc_nan_vel / game.finite_vel (no claim
left on those lines: labcov.CLAIM finds none).

## History
- 2026-11-20: opened.
