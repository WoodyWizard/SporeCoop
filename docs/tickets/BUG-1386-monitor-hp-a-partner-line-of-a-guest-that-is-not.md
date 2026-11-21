---
id: BUG-1386
title: monitor hp: a partner line of a guest that is not in a world (flags 0x0, hp 0/0, reloading) is compared with the guest's own line of its next session
status: open
kind: tooling
stage: All
reported: 2026-11-20
source: lab rel/0.7.16 20261120-133703 cell_rejoin_leave
role: 
versions: unreleased
fixed_in:
branch: fix/w61-cell
commits: edf2228d
tests: cell_rejoin_leave, gates_selftest
signature:
docs:
related: BUG-1061, BUG-1194
---

## Report
0.7.16 second final check (lab 20261120-133703, lane 2, cold): cell_rejoin_leave RESULT OK, but the lab failed it on
`gate monitor: 1 on host (hp): hp: host has guest at hp 0/0, guest's own game 6/6 (s 80.004)`.

## Root cause
Observed (logs): the guest leaves and joins 12 times; the host's partner lines at s 60, 70, 80 say `puppet=hidden: partner
not in a world hp=0/0 flags=0x0` (the guest was reloading the shared world), while the guest's own lines at s 70.05 /
80.05 say Galaxy menu. The Hp invariant takes the subject's nearest own line of the same stage and world (BUG-1061), so
those menu lines were skipped and the guest's Cell lines of its next session (s 73.3 / 83.3, 6/6) were compared: two in
a row, a violation. A partner that is not in a world has no hp; the host's record was right. A gate fault (`monitor.py
replay` of the two logs: the violation with the old code, none with the fix).

## Fix
tools/tests/monitor.py Hp: a partner line without PF_IN_WORLD (flags bit 0) is not compared (as the puppet invariant
already does). gates_selftest: a partner not in a world with hp 0/0 is no violation; hp 0/0 with the partner in the
world still is one (FAIL on the old monitor, PASS on the new).

## Verification
no-fail-run: gate-only change, proved by `make test` (gates_selftest hp_not_in_world: FAIL on the old monitor.py, PASS on
the new) and `monitor.py replay` of the run's logs (old: the hp violation, new: none). Lab: `SPORE_DETECTOR=20 python3
tools/lab.py run cell_rejoin_leave --lane N --retry 0 --no-build` PASS. Open, waiting for the lab run.

## History
- 2026-11-20: opened from the second final check of 0.7.16; fixed in the monitor on fix/w61-cell.
