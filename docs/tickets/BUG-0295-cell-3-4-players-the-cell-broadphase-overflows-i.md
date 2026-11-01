---
id: BUG-0295
title: Cell, 3-4 players: the cell broadphase overflows its 4096-endpoint stack buffer when a game holds over ~2000 cells (fault at dc04aee5:Simulator::cStarRecord::GetPlanetRecord+0x4AB, 0x00BBB07B)
status: fixed
kind: crash
stage: Cell
reported: 2026-10-30
source: lab 20261030-035318 campaign#2/#3
role: 
versions: 0.7.3.1+main 1d51511
fixed_in: 0.7.3.2
branch: fix/w10-cell-4p
commits: 08428c0, f34dad4, 3a3292b, 1ebfd3e, e1962bd, 878a768, 6a731e6, bea15ec
tests: cell_4p, campaign_short
signature: dc04aee5:Simulator::cStarRecord::GetPlanetRecord+0x4AB (0x00BBB07B, really SweepPrune::MergeAxis+0x16B)
docs: docs/re/cell.md#cell-broadphase-cap, docs/known-issues.md#issue-cell-4p-budget, docs/06-testing.md#testing-cell-4p
related: BUG-0174, BUG-0296, BUG-0297, BUG-0298, BUG-0299
---

## Report
Final 4-player campaign on main 1d51511 (lab 20261030-035318, `--lane 2 --lane 4 --lane 7 --repeat 3`): in campaign#2 and #3 every game (host and three guests) faulted at 0x00BBB07B within 2 s at level 4 of the tile levels, then crashes (BUG-0296, BUG-0297), faults at small addresses (0x9BB) and consoles that stopped answering. The pools held 1950-2450 live cells (`cell pool` in the tiles trace). A/B by the coordinator: `SPORE_FIX_OFF=cell.partner_tiles` campaign_short 4/4 OK (lab 20261030-051633).


## Root cause
Observed (Ghidra, GOG 2024): Simulator::Cell::SimUpdate+0x131 calls the cell broadphase SweepPrune::UpdatePairs 0xBBB450, whose MergeAxis 0xBBAF10 keeps displaced endpoints in a stack buffer of 4096 entries (2 per cell) with no bound check. More than ~2048 live cells overrun it: the fault at MergeAxis+0x16B (EAX, the merge index, 0x1021..0x1084 in the logs), symbolised as GetPlanetRecord+0x4AB; the stack and heap are corrupted after it. Observed (logs, cell_4p with the bound off): BUG-0174's partner passes (a screen of tiles per partner) and each guest's 120 m state (every host cell near any partner) grew the host's pool to 4096 and the guests' to 2200-3100 with four players. With two players (w9 tests) the pools stayed far below.


## Fix
Switch `coop.fix cell.budget` (cellsync.cpp): while partners play, a gameplay tile on a player's screen (the host's or a partner's) is populated up to 1300 live host cells, a tile of the ring around a screen only below 1100; the others wait as "starved" tiles (remembering their player) and are fed by the same rule when room comes back; from 850 cells at most 2 partner tiles per frame (a tile's cells reach the live count over the next frames); the partner passes take turns; each guest gets at most its 900 nearest cells; a guest creates no new mirror at 1500 live cells. `coop.pool` prints budget, starved, fed, screen, send_capped, mirror_capped, cells, peak, level. Limitation (known-issues#issue-cell-4p-budget): with four players at high levels the rings around the screens are thinner (food appears at a screen edge a few frames late).


## Verification
FAIL: `SPORE_FIX_OFF=cell.budget` cell_4p, lab 20261030-052851 (host pool 4096, guests up to 3140, 8203 faults on the host) and on the final code lab 20261030-072434 (host pool 4096, faults at SweepPrune::MergeAxis+0x16B and UpdatePairs+0xD9, the host died).
PASS (final code bea15ec): cell_4p lab 20261030-065411 (3/3, lanes 2/3/7: host peaks 1386-1430, guests <= 1500, every guest sees host cells on its screen, no fault); the area's tests lab 20261030-070300 (lane 4).
The campaign FAIL on main: lab 20261030-035318. PASS campaign_short with 4 players, final code bea15ec: lab 20261030-070059 (3/3 OK, lanes 2/3/7, no fault, no crash, no coverage finding). Earlier on a964c90: lab 20261030-060122 (Cell passed 3/3; #3 failed later in Creature, BUG-0300) and 20261030-061151 (2/2 OK).
Steps on the way: cap 1400 -> host peak 1714 (lab 20261030-053258); a far guest's screen empty without the screen rule (lab 20261030-054633), without feeding by the same rule (lab 20261030-060124) and without the host's own tiles under the budget (lab 20261030-064009); 1250 / 1500 -> peaks 1759-1776 (lab 20261030-063326).


## History
- 2026-10-30: opened.
- 2026-10-30: fix/w10-cell-4p: root cause (broadphase stack buffer), budget fix, cell_4p; fixed (unreleased).
- 2026-11-01: released in 0.7.3.2.
