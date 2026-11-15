---
id: BUG-0821
title: Cell: the host's kill ledger records a guest's kill before refusing it: that cell cannot be killed by guests for 10 s
status: fixed
kind: bug
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-cell
commits: 3e554da8
tests: cell_kill_refused
signature:
docs: docs/re/cell.md#cell-c1-0713
related: BUG-0632 BUG-0509
---

## Report
No player report. After a guest's kill the host refused (sent while knocked out, or another guest's kill of the same cell with 3-4 players), no guest can kill that creature for 10 s.

## Root cause
Observed (lab 20261112-070304, `cell.kill_refused` off): a knocked-out guest's lethal bite in flight (`coop.pool
sendact`) was refused by the host (BUG-0509 / BUG-0714), the host revived the guest at once, and the guest's next
real lethal bite of the same creature (the game's Bite on its mirror, 1-2 s later) was refused "killed here
already": the creature lived on in the host's game for the 8.5 s watched. Cause (code): the MSG_CELL_EAT handler wrote
the creature into g_kill_applied (10 s) before its knockout refusal. On the guest g_kill_sent likewise held a kill the
host never made for 10 s (code; not separately measured). The "game spares it" reading does not happen through hp:
guest_attack raises a lethal bite's damage to the host's hp.

## Fix
cellsync.cpp `cell.kill_refused`: the host only looks the creature up in g_kill_applied and records it after a kill
it applied that left the creature dying (dying, dead, hp 0, or state 7 / 8 / 9); a guest drops its g_kill_sent entry
and sends again when the creature still lives 2 s after its last send (kill_send_first; `cell_c1: kills_resent=`).
Switch `coop.fix cell.kill_refused off` (0.7.12).

## Fix idea
Record in g_kill_applied only after the kill was applied (the victim dying); on the guest drop the g_kill_sent entry when the host's state still shows the cell alive after a round trip.

## How to test
Guest `coop.pool kodrop` / `sendact` to send a kill while knocked out, revive, bite the same cell to death: the host applies it (fix) vs 'refused (killed here already)' (main).

## Verification
`coop_cell_kill_refused.py` (suite `cell_kill_refused`, ~1 min): the refused kill of a knocked-out guest, the revive,
then the guest's real lethal bite must kill the creature in the host's game.
* FAIL with `SPORE_FIX_OFF=cell.kill_refused`: lab 20261112-070304 (alive 8.5 s after the refused kill).
* PASS: lab 20261112-070216, 20261112-070847.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-cell): fixed (cell.kill_refused), FAIL and PASS runs in Verification.
- 2026-11-12: released in 0.7.13.
