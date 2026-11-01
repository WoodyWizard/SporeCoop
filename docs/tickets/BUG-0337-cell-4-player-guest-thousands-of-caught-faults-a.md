---
id: BUG-0337
title: Cell 4-player guest: thousands of caught faults at small-integer EIPs, then freeze
status: fixed
kind: crash
stage: Cell
reported: 2026-10-31
source: lab 20261031-054758 campaign#1
role: guest
versions: 0.7.3.1
fixed_in: 0.7.3.2
branch: fix/w20-cell
commits: a649261, ad2d187, 0181edb, 46b2667
tests: cell_sap
signature: dc04aee5: faults at small addresses (0x196, 0x4D6, 0x144, ...) with esp=0012D394 edi=01639470 on the main thread, really the ret of SweepPrune::UpdatePairs (0xBBB450) to an overwritten return address; stale frames named UIMissionCardPanel_HandleMessage+0x775 < App::cCellModeStrategy::OnExit+0xC4C
docs: docs/re/cell.md#cell-sap-active-list, docs/known-issues.md#issue-cell-sap-broken-order, docs/06-testing.md#testing-cell-4p
related: BUG-0295, BUG-0338
---

## Report
Final verification of 0.7.3.2 on an idle machine (lab 20261031-054758, campaign#1, lane 2, four
players): in Cell, a guest (Carol, id 2) started at 06:55:05 (game clock) to fault thousands of
times (4314 in all) at small addresses (0x0, 0x196, 0x4D6, 0x144, 0x17, 0x123, 0x513, ...), always
esp=0012D394 ebp=034CF480 esi=0EE0D0B8 edi=01639470, each fault with a 500-750 ms hitch; then a
Cell desync (c 2.3), the stage change, and the guest's game froze ("no frame for 10 s") and
stopped answering its console. Just before: 927 cells in its pool, 900 mirrors, 4 players.

## Root cause
Observed (Ghidra, GOG 2024; the registers of the logs; the same signature made on purpose):
Simulator::Cell::SimUpdate+0x131 calls SweepPrune::UpdatePairs 0xBBB450 (the cell broadphase),
which sweeps along x with the open objects in an int[1024] at esp+0x1C of its 0x100C-byte frame,
no bound check. open[1024] is its return address: with more than 1024 objects open at once `ret`
jumps to an object id (the faulting EIP), esp ends 4 above the slot (0012D394; [esp] = open[1025],
0x456, 0x144, ...), the callee-saved registers are SimUpdate's (they are below the list). The game
catches the fault and skips the rest of SimUpdate, every frame. `coop.pool column 1100` on a host
with ~1300 cells gives exactly this (fix off: 2628-2680 faults in 40 s at small EIPs with
esp=0012D394, the host's cell stops, lab 20261031-080907-fix-w20-cell). The nearest-symbol names
of the crash tools are stale return addresses (0xE18AD5 in FUN_00e18aa0, 0xE8084C = SimUpdate's
caller), no message handler is involved, and no count of the mod reached a cap.
Inferred (not reproduced): why the guest had over 1024 open objects with 927 cells. EDX at its
faults (the open count at the end of the sweep) was 0x3F9-0x407: ~1024 objects never closed, more
than its live objects, so its endpoint arrays held entries left behind or out of order. Three
replays of the campaign's Cell stage (seed 755764009, lanes 5 / 6, `coop.pool sap` every 10 s on
all games) showed 13-43 open at most, none left at the end, no duplicate or lone entries. The same
frame (EDX 0x614 / 0x7F0) is in lab 20261030-035318 right after BUG-0295's MergeAxis overrun.
Open follow-up: docs/known-issues.md#issue-cell-sap-broken-order.

## Fix
`coop.fix cell.sap_sweep` (cellsync.cpp `h_UpdatePairs`, a hook on 0xBBB450, GOG 2024 only): the
same two SweepPrune::MergeAxis calls, the same sweep with the open list in a heap vector, the
game's pair test (SweepPrune::OverlapOther 0xBBB110) and the same pairs in the same order. A sweep
over 1024 open objects logs once; more than 64 left open at its end logs the `coop.pool sap` report
(at most every 30 s), so the next broken order is recorded. Unsafe off side (R4: it overwrites the
stack). Test helpers: `coop.pool sap` (replays the game's sweep, prints our counters), `coop.pool
column <n>`.

## Verification
FAIL: `SPORE_FIX_OFF=cell.sap_sweep` cell_sap, lab 20261031-080907-fix-w20-cell (lane 5: the host
faults 2628 times at small EIPs with esp=0012D394, its cell does not move, no guest sees it move);
also 20261031-075501-fix-w20-cell and 20261031-080143-fix-w20-cell on earlier versions of the test.
PASS: cell_sap 2/2, lab 20261031-080912-fix-w20-cell (lane 6: sweep_peak 1050 / 1075, the host's
cell swims, all three guests see it swim, no fault on any game).
The campaign's own case was not reproduced (see Root cause).

## History
- 2026-10-31: opened.
- 2026-10-31: fix/w20-cell: root cause (UpdatePairs' open list on the stack), cell.sap_sweep, cell_sap; fixed (unreleased). The broken order on the guest stays open (known-issues).
- 2026-11-01: released in 0.7.3.2.
