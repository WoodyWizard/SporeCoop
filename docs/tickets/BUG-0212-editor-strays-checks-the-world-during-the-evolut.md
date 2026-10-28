---
id: BUG-0212
title: editor_strays checks the world during the evolution cinematic since BUG-0019 delays the share
status: fixed
kind: test
stage: Creature
reported: 2026-10-28
source: lab runs 20261028-043811, 20261028-051522
role: both
versions: unreleased
fixed_in: unreleased
branch: fix/rel-strays
commits: 76c47f7
tests: editor_strays
signature:
docs: docs/06-testing.md#testing-editor-strays-cinematic
related: BUG-0019, BUG-0018, BUG-0007, BUG-0022
---

## Report
`coop_editor_strays.py` (suite editor_strays) failed every time on main 1d4cd27 before the 0.7.3.0
release (lab runs 20261028-043811, 20261028-051522 2/2, and 20261028-052236-fix-rel-strays on lane 2):
* `FAIL guest after evolution 1: the homes of our herds are on the surface (planet radius 0, homes [])`
* `FAIL host after evolution 2: no puppet left over as a creature of its own ['creature ... puppet=0 ...']`
It passed at v0.7.2.4 (lab 20261025-151314).

## Root cause
Test timing, not a regression of the game code (observed in the host and guest game logs).
BUG-0019 (`npc.sharecine`, wave 2) makes the host send its world after an evolution only when the
evolution cinematic (CRG_SpeciesMigrate) is over: about 10 s after the host left the editor, where
v0.7.2.4 sent it at once (`sending the world to Bob` in the same millisecond as `evolved the species`).
The test checked 4 s + `wait_together` + 5 s after the editors, i.e. inside that window:
* the guest got the world just then and was in its galaxy menu loading it: `coop.npc herds` there has
  no avatar (|pos| 0) and no herds, so "planet radius 0, homes []";
* the host's left-over puppet was removed through `game::destroy_creature`, which holds creatures
  while a cinematic plays (BUG-0018, deferred destroy): the log said "removed" but the creature
  still stood until the cinematic ended. No deferred destroy was dropped by the world counter
  (BUG-0119): a log line added to that path never fired.
After the cinematic both are right (the run with the wait passes; with the removers off it fails).

## Fix
The test waits after each evolution until the host's `for shares` counter went up, no cinematic
plays on the host, no deferred destroy is pending, the guest's log has a new
`world: loading <planet> from` and the guest is back in the creature stage, then checks.
New switch `coop.fix core.strays off` (BUG-0007 left-over puppet removal), so the test can be seen
failing: `SPORE_FIX_OFF=core.strays,npc.strays`. game.cpp logs a deferred destroy dropped because
the world changed (diagnostics).

## Verification
* FAIL before (main code, old test): 20261028-052236-fix-rel-strays (both FAIL lines as on main).
* FAIL with the removers off (new test, `SPORE_FIX_OFF=core.strays,npc.strays`):
  20261028-052840-fix-rel-strays (left-over puppets on the host after evolution 1 and 2).
* PASS: 20261028-052551-fix-rel-strays, 20261028-053530-fix-rel-strays (with creature,
  creature_desync, save_hygiene, cinematic_puppet, creature_e84: 6/6), lane 2.

## History
- 2026-10-28: opened and fixed on fix/rel-strays (test waits for the cinematic and the share; core.strays switch).
