---
id: BUG-0352
title: Cell: after a world reload without an own-cell rebuild a guest starts at the pool's start, up to 130 m from the host
status: open
kind: bug
stage: Cell
reported: 2026-10-31
source: player reports 2026-10-31 (second batch: 11 zips, host and guest)
role: guest
versions: 0.7.3.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/cell.md#cell-partner-tiles
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
Not done. Proposal: on the guest, after any world load into Cell (new avatar index), move the own
cell next to the host once the pool has settled (join_partner), not only after an own-cell rebuild.

## Verification
Test to write: host swims 100 m from the start, shares the world; the guest's avatar is within 10 m
of the host's cell 5 s after the guest's load (game positions on both machines). Must FAIL on main.

## History
- 2026-10-31: opened from the guest's log of the second batch of the 2026-10-31 report zips.
