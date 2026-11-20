---
id: BUG-1330
title: Tests: coop.pool near of BUG-1309 shadows BUG-1246's coop.pool near, evo_screens_cell_mate reads the wrong list
status: open
kind: tooling
stage: Cell
reported: 2026-11-20
source: triage
role: 
versions: 0.7.16
fixed_in:
branch: fix/w59-cell
commits:
tests: evo_screens_cell_mate, cell_slow_link, cell_slow_link_off
signature: evo_screens_cell_mate "the host's game draws the guest's mate where the guest's game has it (0 of 6 looks)"
docs: docs/06-testing.md#testing-cell-slow-link
related: BUG-1246, BUG-1309
---

## Report
The 0.7.16 final check (rel/0.7.16 f6c16890, out/lab/20261119-214823, lane 2): evo_screens_cell_mate FAILED with
"the host's game draws the guest's mate where the guest's game has it (0 of 6 looks)", while the host's own
`coop.pool mate` status shows `copy(... alive=1 gfx=1 at=(102.8 104.7) model=4DA56CA5)` right where the guest's mate
is. Triage: out/TRIAGE-0.7.16.md (package B).

## Root cause
A test command collision, not the mod (read from the code). BUG-1309 (fix/w57-cellslow, eb50b7d9) added the
sub-command `coop.pool near <r> [x y]` (cellsync_debug.cpp, the guest's mirrors keyed by the host's index) in front of
BUG-1246's older `coop.pool near <x> <y> <r>` (the cells drawn at a place with model, gfx, puppet, avatar). The first
`near` matches every call, so BUG-1246's handler was never reached again. coop_evo_screens.py `mate_seen()` called
`near x y 5` and got `near n=.. keys ..` with no `model=` line: 0 of 6 looks, and the "the guest has no second copy"
check passed for the wrong reason (it found no line either).

## Fix
BUG-1309's sub-command is now `coop.pool nearkeys <r> [x y]` (output `nearkeys n=...` + `keys`); BUG-1246's
`coop.pool near <x> <y> <r>` is reachable again. Callers updated: coop_cell_slow_link.py (its only user), docs
(06-testing.md#testing-cell-slow-link, re/cell.md). Test tooling only, no game behaviour, so no switch.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
