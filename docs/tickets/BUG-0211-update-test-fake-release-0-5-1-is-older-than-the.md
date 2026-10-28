---
id: BUG-0211
title: update test: fake release 0.5.1 is older than the running version, the setup installs the published release, parallel copies share lane and port
status: fixed
kind: test
stage: installer
reported: 2026-10-28
source: lab 20261028-043811, 20261028-051522
role: 
versions: 0.7.2.4
fixed_in: unreleased
branch: fix/rel-update
commits: 8cd95ef, 6cf33b7
tests: update (tools/tests/update_check.py)
signature:
docs:
related: BUG-0068, BUG-0094, BUG-0114
---

## Report
Suite test `update` failed on main 1d4cd27 before the 0.7.3.0 release. Full run
out/lab/20261028-043811: "the game finds SporeCoop 0.5.1", "the co-op window shows an Update
button", "the update is downloaded, checked and installed", "the version file says 0.5.1", "after
the restart coop 0.5.1 runs" FAIL; the game said `update: up to date running=0.7.2.4 latest=0.5.1`.
Reruns out/lab/20261028-051522 (`--repeat 2`): `update#1` "the setup installed the loader", "and kept
the game's steam_api.dll" FAIL, `update#2` `OSError: Address already in use` on port 47999.

## Root cause
The test, not the product (observed):
* The fake release was a fixed `NEW = '0.5.1'` (written at 0.5.0). Since 0.5.2 it is older than
  the running version, and `autoupdate.cpp` offers only a newer release
  (`compare_versions(latest, running) > 0`, unchanged since f4bc169) and refuses a pack that is not
  newer. "Up to date" was the right answer. Not caused by the wave 2-4 changes (BUG-0094 refuses
  downgrades in the setup only; the in-game check always compared).
* The test never rebuilt `out/dist`: it passed `/pack:out/dist/SporeCoop-<COOP_VERSION>.pack`, which
  did not exist (out/dist held 0.7.0 from 2026-10-24), and the setup then downloaded the published
  0.7.2.4 from GitHub. So it tested the published release and an old SporeCoopSetup.exe, not the
  tree (the "installed version 0.7.2.4" check passed by coincidence).
* `lab.py --repeat 2` gives both copies the same SPORE_FREE_LANE (8) and the server port was fixed:
  one copy's `spore.sh kill` / wipe hit the other's setup run, the other could not bind the port.

## Fix
`tools/tests/update_check.py`: the release under test is COOP_VERSION with the last part + 1
(0.7.2.4 -> 0.7.2.5); `tools/package.sh` runs first so the setup and pack are this tree's; the setup
gets `/api:` of a missing page on the local server (never the published release) and a check that
the pack exists; an flock per lane (`SPORE-lab/.update_check-lane<N>.lock`) taken before anything
touches the lane, and the port is 47999 + lane. No product change: the real setup and in-game
update path work (below).

## Verification
Lane 3, by hand (the lab's SPORE_FREE_LANE would be another agent's lane):
* FAIL: main's update_check.py on this tree (fresh out/dist): "the game finds SporeCoop 0.5.1"
  ... "after the restart coop 0.5.1 runs" FAIL, `up to date running=0.7.2.4 latest=0.5.1`.
* PASS: fixed test, one run: 0.7.2.4 installed by the setup, 0.7.2.5 found, Update button,
  downloaded + checked + installed, after the restart coop 0.7.2.5 runs, uninstall restores
  steam_api.dll.
* PASS x2: two copies started at once on lane 3 (the `--repeat` case): both RESULT OK, one after
  the other (before the lock was moved ahead of `spore.sh kill`, the second copy killed the
  first's setup: "setup: (no log)").
* win_unit, join_name: out/lab/20261028-052946-fix-rel-update PASSED 2/2.
Players were not affected.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/rel-update (test only; the product's update path passes end to end).
- 2026-10-28: the final full run (out/lab/20261028-070630) failed update and win_unit together: update's package.sh rebuilt build/ while win_unit ran build/SporeCoopSetup.exe on another lane. Both pass alone (update 2/2, win_unit 3/3). `update` is now exclusive (runs alone at the end of a lab run).
