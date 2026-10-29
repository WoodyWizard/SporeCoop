---
id: BUG-0006
title: Guest crash on session end after the Cell stage (coop modes::cell_at)
status: fixed
kind: crash
stage: Creature
reported: 2026-10-25
source: Spore Exception Report (guest, 14:13, 0.7.2.1)
role: guest
versions: 0.7.2.1
fixed_in: 0.7.2.3
branch: fix/crash-reports-0725
commits: fa68390, 6bc1766
tests: cell_leave
signature: coop.dll modes::cell_at <- cellsync::destroy_local <- on_session_end
docs: docs/re/cell.md 13
related:
switch: pool.forget
---

## Report
The guest crashed 30 s after the host's game crashed, in the Creature stage.

## Root cause
cCellGame outlives the Cell stage with its pool freed; cellsync kept ~80 mirrors and destroyed them at session end in the dead pool.

## Fix
Mirrors and puppet indices are forgotten once the Cell stage is left; destroy only in the stage; cell commands answer only in the stage.

## Verification
coop_cell_leave.py FAIL on 0.7.2.2 (same signature), OK 3x after.

## History
- 2026-10-25: reported (Spore Exception Report (guest, 14:13, 0.7.2.1)).
- 2026-10-25: fixed on fix/crash-reports-0725, released in 0.7.2.3.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: switch: pool.forget (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
