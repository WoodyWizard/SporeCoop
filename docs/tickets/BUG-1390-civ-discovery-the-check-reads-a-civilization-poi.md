---
id: BUG-1390
title: civ_discovery: the check reads a civilization pointer that starts with 0 as null (test bug)
status: fixed
kind: test
stage: Civ
reported: 2026-11-21
source: lab 20261121-003202
role: test
versions: 
fixed_in: 0.7.16
branch: fix/w62-flake-tcs
commits: 69313126
tests: civ_discovery
signature:
docs:
related: BUG-0492
---

## Report
civ_discovery FAILED in the third final check of 0.7.16 (lab 20261121-003202, lane 6): "FAIL the guest's game met a
civilization it has (the game's own path, no crash)" while the game answered `meeting 000021CB (civ=0D595428)` and the
planner line had `met=0`. The history calls it a known flake (2 of 11 on rel/main): the other FAIL run is
20261103-204638 (rel/0.7.4), `civ=0DB93420`.

## Root cause
* Observed (the test, tools/tests/coop_civ_discovery.py): the "met a civilization it has" check was
  `'civ=0' not in out`, meant for a null pointer (`civ=00000000`). The pointer is printed with %p (8 hex digits), so
  every civilization object below 0x10000000 starts with 0 (`civ=0D595428`) and reads as null.
* Observed (every civ_discovery log of all worktrees): the 2 FAIL runs are exactly the 2 runs whose pointer starts
  with 0 (0D595428, 0DB93420); the 12 passing runs had 10064FA8 .. 1F624C58. Where the game's heap puts the
  civilization decides the result: a test bug, not a flake under load and not the mod.
* Old: in the test since BUG-0492 (0.7.4). Not a w61 regression (no w61 commit touches the test or civ_debug.cpp).

## Fix
The test parses the printed pointer as a number (`civ=([0-9A-Fa-fx]+|(nil))`) and checks it is not 0; the check
message prints it. The check is as strict as meant (null still fails), only the misreading is gone. No mod change, no
switch (test-only).

## Verification
FAIL with the old check: 20261121-003202:civ_discovery and 20261103-204638:civ_discovery (civ=0D595428 /
civ=0DB93420, a civilization that exists). The new parse on the logged answers: `civ=0D595428` -> met (pass),
`civ=00000000` / `civ=(nil)` -> null (fail), `civ=1F624C58` -> met.
no-fail-run: test-only fix of a check that read a valid pointer as null; open, waiting for the lab run (no game today,
owner rule). Tonight: `SPORE_DETECTOR=20 python3 tools/lab.py run civ_discovery --lane N --retry 0 --no-build` must
PASS; the log line "met a civilization it has (... civ XXXXXXXX)" shows the pointer.

## History
- 2026-11-21: opened.
- 2026-11-21 (fix/w62-flake-tcs): cause found (the substring check), test fixed.
- 2026-11-23: released in 0.7.16.
