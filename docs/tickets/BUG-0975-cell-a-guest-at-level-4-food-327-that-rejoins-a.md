---
id: BUG-0975
title: Cell: a guest at level 4 (food 327) that rejoins a host world at food 0 dies in ShouldCullCell (0xC0000005 at 0x00E5FA50, PopulateTiles) right after the join
status: fixed
kind: crash
stage: Cell
reported: 2026-11-12
source: feat/lab-speed lab 20261112-1903 (warm reset after cell_particles, lane 6)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w48-cellcull
commits: 8dd800ce, 561bc917
tests: cell_level_drop, cell_level_drop_solo
signature: 0xC0000005 at 0x00E5FA50 Simulator::Cell::ShouldCullCell+0x190 (data 0x00130000) < PopulateTiles+0xFD
docs: docs/re/cell.md#cell-level-fall, docs/known-issues.md#issue-cell-join-lower-level, docs/06-testing.md#testing-cell-level-drop
related:
---

## Report
Found by feat/lab-speed while making cell tests warm (lab 20261112-190142, lane 6, GOG 2024 build). cell_particles passed (cold),
its games stayed up at level 4 (food 303-327). The next test's warm reset (cell_ko_food: `coop.leave`, the host loads
the `cell` fixture into a fresh slot, food 0, level 1, and the guest joins again) killed the guest right after the
join: the guest's log ends with

    eat: host total food=0 (ack meal 0) -> -327 here (pending 0)
    dna: food 327 -> 0 cause=meal:255-327
    [ERR] sporekit: exception 0xC0000005 at 0x00E5FA50 Simulator::Cell::ShouldCullCell+0x190 (data 0x00130000) tid=304
          eax=FFFFFFF9 ebx=FFFFFFEA ecx=FFFFFFC8 edx=FFFFFFCE esi=00000001 edi=0012FFF4
      called from: Simulator::Cell::ShouldCullCell+0x200 < Simulator::Cell::PopulateTiles+0xFD

and nothing more (no crash report: the process was gone, the console answered "Connection reset by peer"). twoinst
fell back to a cold start and cell_ko_food passed, so the lab did not see it. The two game logs are in
out/lab/20261112-190142/evidence/ (not in git). The player path is the same: a guest that played up the Cell levels joins (or
rejoins) a host whose world is at a lower level (a new game, an older save).

## Root cause
Observed (Ghidra and lanes 6 / 7, docs/re/cell.md#cell-level-fall). The game's own bug, reached only through the mod:
PopulateTiles 0xE7D370 writes its active set (0xE5FA80, nine groups of tiles) into a stack buffer of 511 entries
without a bound. The tiles' size is the level's tile size divided by the world scale G+0x514C, which eases to the new
level's over ~3 s after AddFoodPoints. A fall of three levels at once (food 330 -> 0) keeps the level 4 scale (15)
for the level 1 tiles: ~14 300 tiles, the write loop 0xE5FA50 runs off the stack top (0x00130000) and the process
dies without a crash report (the symbolizer names the place ShouldCullCell+0x190). The same fall in a game with no
session (`coop.pool food -330`, the plain AddFoodPoints) dies the same way, so it is not mod state; the game itself
never lowers food by more than 50 (debug keys), the mod's shared food total does.

Players reach it: a guest that is in a Cell game at a level two or more above the host's and joins (the guest rejoins
in its own copy of the stage and takes the host's total within 0.5 s, before the host's world arrives): a guest who
played on alone after Leave, then rejoins a host that loaded an older or new Cell game; a guest that joins from its own
further Cell save. The 27 lab games lost in warm resets (25 in Cell, after a test that left a higher level) are this
fall (one shown: cell_particles -> cell_ko_food).

## Fix
`cell.tile_cap` (cellsync.cpp h_PopTiles, cell_logic::active_tiles, `Simulator::Cell::TileSizes`): no tile pass while
the world scale still eases down after a fall, or while its active set (computed as the game does) would exceed 480
tiles. After ~2.5 s the settled pass populates the new level as usual. A first version that only capped the set at
480 kept the guest in a session alive, but a game alone then populated 3-6 x the settled set at once and faulted in
CreateSoftBody / CellPhysicsStep (2138 faults, lab 20261112-195008); hence the hold while the scale falls.

## Verification
Test `cell_level_drop` (the guest at food 330 rejoins the host at food 0; the guest's game must answer, take the host's
food and keep its cell; no fault on either game) and the control `cell_level_drop_solo` (the same fall, no session).
FAIL (SPORE_FIX_OFF=cell.tile_cap): out/lab/20261112-194555 (cell_level_drop: the guest gone at the join, 0xC0000005
at 0x00E5FA50 from PopulateTiles, as in the lab-speed run), out/lab/20261112-194555-2 (cell_level_drop_solo: the same
fault in a game alone). PASS (cap only, first version): out/lab/20261112-194718 (cell_level_drop 2 of 2; the solo case
then faulted, out/lab/20261112-195008). PASS (final): out/lab/20261112-195306 (cell_level_drop 2 of 2,
cell_level_drop_solo 2 of 2, 0 faults; 151-303 passes held, the set up to 14 368 tiles, ~2.5-5 s).
Unit: cell_tests active_set.

## History
Earlier runs: the recorded test logs (main checkout and worktrees, 2026-11-02 .. 11-12) have 27 more
`(warm reset failed: [Errno 104] Connection reset by peer )` / `console closed the connection` lines, each a game
gone in a warm reset that twoinst covered with a cold start: cell_coop 18, play_cell (#2, #3 of --repeat) 6,
cell_guest_bite 1, npc_herd_identity 2 (Creature); 25 of 27 in Cell, after a Cell test that left a higher level. Their
game logs were not kept (the restart moves them to logs/previous, which is pruned). Since feat/lab-speed the lab fails
a test whose warm reset lost a game and copies logs/previous into the run folder (`<test>.previous.*`); 19 lines
`warm reset failed: timed out` are only noted (a slow game).

## How to test
Two games in the `cell` fixture, the host `coop.pool food 330` (level 4 on both), then the host loads the fixture
again into a fresh slot (twoinst's warm reset: `SPORE_WARM=1` after `coop_cell_particles.py`) and the guest joins:
the guest must live. cell_particles stays a cold test until this is fixed (suite.py).

## History
- 2026-11-12: opened (feat/lab-speed).
- 2026-11-12: fixed on fix/w48-cellcull (`cell.tile_cap`): the game's own bug, reachable by players through the shared food total.
