---
id: BUG-0975
title: Cell: a guest at level 4 (food 327) that rejoins a host world at food 0 dies in ShouldCullCell (0xC0000005 at 0x00E5FA50, PopulateTiles) right after the join
status: open
kind: crash
stage: Cell
reported: 2026-11-12
source: feat/lab-speed lab 20261112-1903 (warm reset after cell_particles, lane 6)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature: 0xC0000005 at 0x00E5FA50 Simulator::Cell::ShouldCullCell+0x190 (data 0x00130000) < PopulateTiles+0xFD
docs:
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
Not analysed (not feat/lab-speed's task). Observed: the guest's food goes 327 -> 0 in one step at the join (a level 4
to level 1 change of the tiles' world) and the next PopulateTiles faults in ShouldCullCell with a pointer at the stack
top (data 0x00130000, edi 0x0012FFF4): a cell or tile list of the old level read after the change, inferred.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

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
