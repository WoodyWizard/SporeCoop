---
id: BUG-0338
title: Cell: a full soft-body particle pool (1500) breaks its free list: faults every frame at 0x00E4E384 / 0x00E4DBD9, cells thrown thousands of metres
status: fixed
kind: crash
stage: Cell
reported: 2026-10-31
source: lab 20261031-075645-fix-w20-cell cell_sap#1
role: guest
versions: 0.7.3.1+fix/w20-cell
fixed_in: 0.7.3.2
branch: fix/w23-cell
commits: f251d22, d7d6769, e8cbe1a, 835a085, d40438c
tests: cell_particles, cell_sap
signature: dc04aee5:0x00E4E384 (FUN_00e4e320+0x64, symbolised j_mem_free+0x724) under Simulator::Cell::SimUpdate+0xCA; dc04aee5:0x00E4DBD9 (Simulator::Cell::ParticleAlloc+0x39, symbolised GetGlobalsData+0xDB9)
docs: docs/re/cell.md#cell-particle-pool, docs/06-testing.md#testing-cell-4p, docs/known-issues.md#issue-cell-advect-fault
related: BUG-0337
---

## Report
Found while testing BUG-0337 (`coop_cell_sap.py`, fix on, lab 20261031-075645-fix-w20-cell
cell_sap#1, lane 6): the host put 1100 of its cells on one 40 m line next to its own cell
(`coop.pool column 1100`); Bob (a guest 0-50 m from the host, mirroring the pack) faulted 161
times at the same place, `exception 0xC0000005 at 0x00E4E384 (data 0x0000010D)`, esp=0012D2B0,
EDI=FFED2E01, ESI=FFFFFFB4, calls `00E4DDD4 00E4EBED 00E59EFF 00E50917 00E7F46A` (SimUpdate+0xCA).
The second run of the same test had no fault; never seen in any other lab log.
How to reproduce: `SPORE_LANE=N python3 tools/tests/coop_cell_sap.py` with the guests left next to
the host (the test now sends them 200 m away to keep this out of BUG-0337's test).

## Root cause
Observed (Ghidra + runtime): the cells' soft-body particles come from one pool of 1500
(cCellGame+0x4108). Simulator::Cell::ParticleAlloc 0xE4DBA0 returns -1 when it is full; the game
keeps that -1 in the cell's particle list (CreateSoftBody 0xE6D8F0, the re-tier 0xE51A20), reads and
writes particle -1 (8 bytes before the pool's block: a cell's centre from it jumped 2000-29500 m) and
frees it through ParticleFree 0xE4EC10, which has no bound check: -1 becomes the free-list head and
the next allocations read garbage, faulting every frame at 0x00E4DBD9 (inside the allocator) or
0x00E4E384 (the advect query reading the broken particles). Both signatures were made on purpose
with the fix off (coop_cell_particles: 0x00E4E384, 143-360 faults; cell_sap: 0x00E4DBD9).
Can real play reach it? Inferred, not seen: ordinary 2-player play at level 4 peaked at 391
particles on the host; only `coop.pool column 1100` (1100 cells next to the host's cell) filled the
pool (1480-1499). A real crowd of some hundreds of cells near one camera would be needed.

## Fix
`coop.fix cell.particles` (cellsync.cpp, hooks on ParticleAlloc / ParticleFree; unsafe off side): an
allocation the game would refuse gets the spare slot 1499 (the game never hands it out: it refuses at
1499 used), placed at the asking cell at rest; a free of 1499 or of anything outside the pool does
nothing. A first try (giving such a cell no particles from CreateSoftBody by its size) was dropped:
the re-tier gave it particles again a frame later. `coop.pool sap` prints particles=, peak=, spare=,
bad_frees=, minus1_cells=, free_head_bad=; `coop.pool particles <n>` (tests) fills the pool.

## Verification
FAIL: `SPORE_FIX_OFF=cell.particles` cell_particles 2/2 failed, lab 20261031-132955-fix-w23-cell (lane 8:
cells holding -1 in their particle list, the free-list head outside the pool in 10 of 10 rounds, the
guest's own cell 8492 / 29525 m away, 143 faults at 0x00E4E384); earlier 20261031-132707-fix-w23-cell #2.
PASS: cell_particles 3/3, lab 20261031-133000-fix-w23-cell (lane 9); cell_sap 5/5 (lab
20261031-131329-fix-w23-cell and 20261031-131646-fix-w23-cell 4/4); Cell regression 7/7, lab
20261031-131324-fix-w23-cell.

## History
- 2026-10-31: opened.
- 2026-10-31 (fix/w22-cell): seen on the HOST too in cell_sap (lab 20261031-124047-fix-w22-cell, lane 9): 1332 faults at 0x00E4DBD9 (FUN_00e4dba0+0x39, the same advect grid code, data 0x4AF5FEB4), calls 00E5CC88 00E5CCDF 00E6D9D0 00E78CEA 00E7C91A 00E7F502 (SimUpdate+0x162 -> PopulateTile -> DestroyCell path), after `coop.pool column 1100`. The artificial pack of 1100 cells trips it; cell_sap is flaky until this is fixed. Not related to the broadphase fixes (BUG-0337 / BUG-0348).
- 2026-10-31: fix/w23-cell: root cause (the soft-body particle pool's -1), cell.particles, cell_particles; cell_sap 5/5; fixed (unreleased).
- 2026-10-31 (fix/w26-cell-spare): CORRECTION: ParticleAlloc does hand out slot 1499 (at 1498 used, index = the new count); the spare is now that slot alone (cell.spare). Real play filled the pool through a leak, not a crowd of particles: a full cell graphics pool (256) makes CreateSoftBody lose a cell's particles every frame (BUG-0355, cell.gfx_full); the shared spare then went NaN and spread.
- 2026-11-01: released in 0.7.3.2.
