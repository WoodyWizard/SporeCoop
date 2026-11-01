---
id: BUG-0352
title: Cell: after a world reload without an own-cell rebuild a guest starts at the pool's start, up to 130 m from the host
status: fixed
kind: bug
stage: Cell
reported: 2026-10-31
source: player reports 2026-10-31 (second batch: 11 zips, host and guest)
role: guest
versions: 0.7.3.1
fixed_in: 0.7.3.2
branch: fix/w22-reports2
commits: 6f18d53, d12698c
tests: tools/tests/coop_cell_reload_join.py (suite cell_reload_join)
signature:
docs: docs/re/cell.md#cell-join-after-load, docs/06-testing.md#testing-cell-reload-ring
related: BUG-0174, BUG-0349
---

## Report
Seen in the guest's log while triaging the 2026-10-31 reports (no player mark of its own): after the
host shared the world again (19:18:54 host time) the guest loaded it and started 131 m from the
host, outside the host's view (`host d=131 (outside the ...)`, `plants=0 creatures=0`), and stayed
so for 50 s until the host was knocked out and respawned next to the guest.

## Root cause
Observed: after a load the guest's avatar starts at the pool's start (pos (100 100 0), the same as
the host after an editor). `cellsync::join_partner()` (stop_at next to the nearest partner) is called
only from `creations.cpp apply_own_cell` after an own-cell rebuild (`C.cell_rebuilt`), so a load
without that rebuild (BUG-0349: every load after the first one of a session) leaves the guest at the
start. Loads at 19:12:51 (host also near the start after its editor: d=13) and 19:15:59 (rebuild:
joined) were fine; 19:19:04 was not.

## Fix
`cell.join_after_load` (cellsync.cpp `join_after_load`, guest only): every new own avatar index (a
load, an editor's return, a rebuild) goes 2.5 m next to the host once the game's input lock (the
hatch after a load) is off, the host's state is here and the cell is 15 m or more away (2 s to 30 s
after the avatar appeared, not while knocked out); 3 s later it checks that the move held and tries
again (at most 3 times). Log `cell: our new cell joined the host (N m away after the load, try N)`.
Found on the way: a move made during the hatch (2 s after the load, input lock on) is undone by the
game; the first version of the fix did that and failed its test.
`coop.fix cell.join_after_load off`: only after an own-cell rebuild (0.7.3.1).

## Verification
`coop_cell_reload_join.py` (cell fixture, lane 3): host 80 m away, save, share, guest reloads.
* FAIL with `SPORE_FIX_OFF=cell.join_after_load`: out/lab/20261031-131902 (guest 81 m from the host
  20 s after the load in both games), also 20261031-131104 (first test version).
* PASS: out/lab/20261031-131706 (2 runs: the guest 2.6 m from the host 4.9 / 5.3 s after the load,
  both games). The first fix version (join during the hatch) FAILED: 20261031-131228.
Related tests PASS on lane 3: out/lab/20261031-132041 (cell_editor_invuln, cell_guest_bite, cell_rescue,
cell_respawn_swap, creations_cell, cell); `make test` PASS.

## History
- 2026-10-31: opened from the guest's log of the second batch of the 2026-10-31 report zips.
- 2026-10-31: fixed on fix/w22-reports2 (cell.join_after_load).
- 2026-11-01: released in 0.7.3.2.
