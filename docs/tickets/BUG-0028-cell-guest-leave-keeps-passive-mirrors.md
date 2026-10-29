---
id: BUG-0028
title: Cell guest leaving the session retains mirror cells
status: fixed
kind: bug
stage: Cell
reported: 2026-10-27
source: known issue and code review
role: guest
versions: observed 0.7.2.4
fixed_in: 0.7.3.0
branch: fix/cell-guest-leave
commits: 2366d74, 53960f5, 9d287af
tests: cell_coop
signature:
docs: docs/re/cell.md
related: BUG-0006
switch: pool.leave
---

## Report
When the guest leaves in the Cell stage, mirrors of the host's cells can remain in the guest's
pool. Retained cells need not remain passive.

## Root cause
The guest's `do_leave` does not call `cellsync::on_session_end`; the disconnection callback does.

## Fix
Run Cell cleanup when a player leaves voluntarily. The cleanup removes mirrors only while the
Cell pool is valid, then forgets them.

## Verification
Manual test: two Steam Galactic Adventures processes on Windows, SporeCoop 0.7.2.4. With a
baseline DLL using the same code and diagnostics except for the `cellsync::on_session_end` call
removed from `do_leave`, 339 of 341 sampled guest mirror generation indices remained after
`coop.leave`. After hot-reloading the fixed DLL on the guest and rejoining the same baseline host,
0 of 341 remained after leave; the guest was offline, advanced from frame 4021 to 4081 in two
seconds, and reported `exc total faults: 0`. Four retained baseline creatures had AI lock 0 and
nonzero velocity several seconds after leave.

The updated `cell_coop` automation checks sampled local pool indices before and after guest leave;
that updated automated check has not been run as part of this review. The manual test is not a
Linux lab run.

Evidence check (2026-10-28, BUG-0215): no FAIL run of the bug was kept and no lab run shows the old behaviour; the fix was checked by hand only. cell_coop passes in later lab runs but does not switch the fix off.
no-fail-run: manual test only, no run kept

## History
- 2026-10-27: recorded from the known issue.
- 2026-10-27: fixed on fix/cell-guest-leave, not released yet.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: commits: 9d287af (review follow-up) added (BUG-0215).
- 2026-10-28: switch: pool.leave (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
