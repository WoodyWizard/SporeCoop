---
id: BUG-0292
title: Cell: a guest's own cell position became NaN, then its game crashed in PopulateTile+0x8A
status: fixed
kind: crash
stage: Cell
reported: 2026-10-30
source: fix/w9-cell-food lab 20261030-012925 cell_new_game
role: guest
versions: 
fixed_in: 0.7.3.2
branch: fix/w9-cell-food
commits: de617d3
tests: cell_new_game, cell_far_guest
signature: 0x00E7C74A PopulateTile+0x8A (GOG 2024)
docs: docs/known-issues.md#issue-cell-guest-nan-populate, docs/re/cell.md#cell-partner-tiles
related: BUG-0174
---

## Report
Seen once on fix/w9-cell-food (commit dc71981, the partner tiles of BUG-0174 on), lab 20261030-012925,
test `cell_new_game` (lane 2, GOG 2024): 45 s into the play phase the GUEST's game took a handled fault
at `PopulateTile+0x8A` (0xE7C74A, reading the tile level table with a garbage level: the tile object
was garbage, called from PopulateTiles+0x1EF), stood 2 s, and from then on its own cell's position
was NaN (`pos=(-nan -nan)` in every trace line, the host saw the puppet go and dropped its partner
tiles); 40 s later at a level change (food 108) the game crashed at the same address through
RemoveCellsOfOldScale (`crash-20261030-023236-i4`, kept in the run directory). The host had no fault.
Two reruns of the same test on the next build (lab 20261030-013749, `--repeat 2`) passed with no
fault; not seen on main or other branches (searched every lab log).

Inferred, not verified: a NaN view point makes the active tile range of 0xE5F610 garbage
((int)NaN), PopulateTiles then fills its 511-entry stack array with garbage tiles; so the tile fault
is likely a consequence of the NaN position, not its cause. The guest's tile code is unchanged by
BUG-0174 (its hooks pass through on a guest); what changed for the guest is which host cells it
mirrors (those within 120 m of its own cell). Where the NaN came from is not known.
Reproduce: `tools/lab.py run cell_new_game --lane N` (1 of 4 runs so far).


## Root cause
Not confirmed. What could feed a NaN from the new partner cell set (checked, fix/w9-cell-food after
the merge with main):
* a mirror with a NaN / infinite pose sent by the host (every host cell within 120 m of the guest is
  sent; a NaN one makes a NaN mirror, and the guest's own soft body touching it goes NaN): no guard
  existed on either side;
* a partner position that is not finite noted as a view centre (the host's partner box NaN): the
  registry and the Cell `Set` took any value;
* the guest's steering (`v = host vel + (want - cur) * 2`, `want = pos + vel * age`, age <= 0.5 s):
  no division, NaN only from a NaN input.
None of them was seen: no "not finite" line in 9 further runs on the guarded code.


## Fix
A guard, switch `coop.fix pool.finite_state` (on): the host sends no cell whose pose (position,
velocity, size) is not finite, the guest mirrors none (and logs the first of each); a partner
position that is not finite is no view centre (`views::note`, `views::Set::add`), and the host
runs no partner tile pass while its own view is not finite. A guest whose own cell becomes NaN logs
it once with the number of non-finite mirrors (the next report says whether a mirror was the
source). It does not prove the cause.


## Verification
no-fail-run: seen once (lab 20261030-012925), not reproduced since in 7 cell_new_game runs
(20261030-013749 x2, -020044, -022912 #1, -023719 x3) and 8 cell_far_guest runs (015012 x2, 021422,
022912 #1, 023719 x3) on lane 2, none with a fault or a NaN; the guard and its log lines are in
from de617d3 on (021422 and later). Unit test: tests/cell_tests.cpp partner_tiles (a NaN partner is
no centre).


## History
- 2026-10-30: opened.
- 2026-10-30: fix/w9-cell-food: seen once, not reproduced in two reruns; left open.
- 2026-10-30: fix/w9-cell-food: guarded (pool.finite_state, finite view centres), not reproduced in 15 more runs; cause not confirmed.
- 2026-11-01: released in 0.7.3.2.
