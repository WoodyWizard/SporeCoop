---
id: BUG-0411
title: Cell: editor_desync fails on main: after the host's evolution the guest's mating call finds no mate (mate=00000000)
status: fixed
kind: test
stage: Cell
reported: 2026-11-01
source: lab regression (fix/w33-unlocks), lane 6
role: guest
versions: unreleased
fixed_in: 0.7.3.4
branch: fix/w34-cell-mate
commits: f798e20 3d3c42d
tests: editor_desync (tools/tests/coop_editor_desync.py, part 4)
signature:
docs: docs/re/cell.md#cell-mate-button
related: BUG-0004, BUG-0177
---

## Report
`editor_desync` fails at part 4 ("after the host's evolution the guest's own editor still opens") on
lane 6, 4 of 4 runs: on fix/w33-unlocks (20261101-142144, 20261101-150148 x2) and on main f0cf695 itself
(branch at main: 20261101-151114, copied into fix/w33-unlocks' out/lab). Every other part passes (the
guest's own cell editor before, the host's vote and editor, the guest gets and shows the host's new cell,
the Creature part after). The guest's mating call after the host's evolution: `mate_button=1
mate=00000000 alive=0 dist=-1.0`: the game shows the button, but no mate comes (before the host's
evolution the same call gave a mate 11 m away and the editor). flaky.json had editor_desync 7 runs,
0 failed before 2026-11-01.

## Root cause
Not a regression and not the mod: a race in the test, about every second run on lane 6, on
today's main and on the last passing commit alike.

* ✔ observed: the last commit with a pass, 861a539 (fix/w30-progress, lab 20261101-081933 on lane 10),
  fails the same way on lane 6: 2 of 4 (lab 20261101-155217, copied to
  `out/lab/20261101-155217-at-861a539`, `mate=00000000` in #2 and #3). main 1c186c2: 1 of 1 passed
  (20261101-152624), then 1 of 3 failed (20261101-153116 #1). So no bisect: the "4 of 4" were a bad
  streak, the 7 earlier runs a good one. No coop.fix switch is in the path.
* ✔ observed (direct runs with a probe before each press, lane 6): after the host's editor the host
  shares its world again (the save waits ~3 s for the stage to settle); the guest leaves the Cell
  stage ~4-5 s after it got the host's new cell record and is back in Cell ~5 s later, then the cell
  hatches (`input_lock=1`, `cine=1` for ~5-10 s; not every load hatches). Part 4 pressed the heart
  5 s after the guest showed the host's new cell and twice more 4 s apart, waiting only for a caption
  (cine=0, read while still in the old Cell stage or the galaxy menu). Failing run: press 1 in
  `GalaxyGameEntry`, presses 2 and 3 in Cell with `input_lock=1 cine=1`: the HUD took no click,
  0xE760F0 never ran (`G+0x51D4` is 0 in the new Cell game), so `mate=00000000`. Passing runs: no
  hatch after the load, or a press after it.
* ✔ (Ghidra) the HUD button calls 0xE760F0 without a condition (docs/re/cell.md#cell-mate-button),
  so a press that arrives is never refused: nothing in the mod or the game state blocks the mate.
* A player is not hurt: the call is lost only while the screen loads or the cell hatches, and works
  when pressed again after it (part 4 passes every time a press comes after the hatch).

## Fix
Test only (`tools/tests/coop_editor_desync.py`): part 4 waits for the guest's reload of the host's
world (out of the Cell stage and back, up to 40 s); every press waits until the HUD takes a click
(Cell, `cine=0`, `coop.uistate input_lock=0`); when the mate is gone for 3 s (a reload took it, or a
press was lost) the test calls again, as a player would (at most 6 presses). The twin switch is the
test's own `--old-wait` (the old waits). No mod change, so no coop.fix switch.

## Verification
FAIL (the old waits, same as `--old-wait`; lane 6): main 1c186c2, lab 20261101-153116 (#1 FAIL,
`mate=00000000`; #2, #3 passed); the last passing commit 861a539, lab 20261101-155217 (copied as
`out/lab/20261101-155217-at-861a539`: #2, #3 FAIL, #1, #4 passed); earlier 20261101-142144,
20261101-150148, 20261101-151114 (fix/w33-unlocks' out/lab).
PASS (the new waits, lane 6): lab 20261101-160955 (3 of 3; every run had the hatch after the
reload, `input_lock=1`, and the first press after it brought the mate).

## History
- 2026-11-01: opened.
- 2026-11-01: fix/w34-cell-mate: a race in the test, not a regression (the last passing commit fails the same way); fixed in the test (unreleased).
- 2026-11-02: released in 0.7.3.4.
