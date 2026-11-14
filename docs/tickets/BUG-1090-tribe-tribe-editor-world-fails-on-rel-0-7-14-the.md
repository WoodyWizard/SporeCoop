---
id: BUG-1090
title: Tribe: tribe_editor_world fails on rel/0.7.14: the co-op epic is still alive in the host's game after the editor's share save
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-13
source: lab 20261113-183059 tribe_editor_world
role: host
versions: rel/0.7.14
fixed_in: unreleased
branch: fix/w49-edworld
commits: 535d9dbd, b9d5beb7
tests: tribe_editor_world
signature: tribe_editor_world "the guest loaded the shared world after 150 s", host log "evolved in place: Bob keeps its world"
docs: docs/re/tribe.md#tribe-stage-epoch, docs/06-testing.md#testing-w5-world
related: BUG-0217, BUG-0121, BUG-0508
---

## Report
Found by fix/w49-bundle in its regression runs (lane 6 / 7, rel/0.7.14 f7480eae + BUG-1084): `tribe_editor_world` failed 3
times out of 3 on the same check, "FAIL the host's game has no co-op epic left: it left before the share save (BUG-0121
guard ran)": after the host's editor visit and the share ("the guest loaded the shared world after 150 s") the host
still had `live_epics=2 tracked=<epic>` (lab 20261113-183059, 20261113-184244; and 20261113-184241 with
`tribe.away_tree_end` off, so not BUG-1084's change, which runs only on guests). The epic is still tracked through the
editor (BUG-0217's checks pass); only the "left before the share save" step fails. Last PASS seen: fix/w5-world
20261029-003648. No faults in either game.
Reproduce: `lab.py run tribe_editor_world --lane N`.

## Root cause
A test problem, not a regression of 0.7.14 (and not of the game code).
Observed: in the failing runs the guest never got a world after the host's editor: the test's wait "the guest loaded
the shared world" ran out after 150 s with the guest's `resets=0`, and the host log says "evolved in place: Bob keeps
its world" where the last PASS (fix/w5-world 20261029-003648, before 0.7.10) said "sending the world to Bob". BUG-0508
(b3bf9bd2, fcb93d69, in v0.7.10, switch `world.evolve_in_place`) keeps a guest in the same world and stage in place
after the host's editor, so no share save follows the editor and the co-op epic rightly stays alive and tracked in the
host's game (`live_epics=2`, the same `tracked`). The test still expected that share save (the BUG-0121 guard) and
was not run in the lab between 2026-10-29 and 2026-11-13, so the change of 0.7.10 went unseen.
Twin: the unchanged test with `world.evolve_in_place` off PASSES (lab 20261113-185335, lane 6), as is it FAILS (lab
20261113-185355, lane 7).

## Fix
Test only (tools/tests/coop_tribe_editor_world.py): after the host is back from the editor the test waits for either
the editor's own share (the old path, `world.evolve_in_place` off) or the in-place decision (`coop.status` "evolve in
place: kept N"). Kept in place, it checks the epic is still alive and the tracked one, then makes a share due
(`coop.share due`: the real share path with its save, as for a joining guest) and checks the host's game lost the
epic before that save (BUG-0121). A bare `coop.share` is not enough: it resends the last saved files without a save
(lab 20261113-185800, 20261113-185815: the guard did not run). No game code changed; no new game functions (no .sym).

## Verification
* FAIL (old test, rel/0.7.14 code): lab 20261113-185355 (lane 7); old test with `world.evolve_in_place` off PASS: lab
  20261113-185335 (lane 6).
* PASS (new test): lab 20261113-185959 (lane 6), 20261113-190014 (lane 7); with `world.evolve_in_place` off (the old
  share path): 20261113-190206 (lane 7).
* The test still catches BUG-0217: with `tribe.editor_world` off it FAILS (lab 20261113-190151, lane 6: the epic
  untracked in the editor, alive after the share save and after the session).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed in fix/w49-edworld: a test problem (the in-place evolution of BUG-0508); the test makes the share
  save itself.
