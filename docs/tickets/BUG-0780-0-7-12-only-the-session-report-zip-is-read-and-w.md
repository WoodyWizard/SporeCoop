---
id: BUG-0780
title: 0.7.12-only: the session report zip is read and written in one call on the main thread (multi-second stall, suspected)
status: fixed
kind: freeze
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: 
versions: 0.7.12 (rel/0.7.12, unreleased)
fixed_in: unreleased
branch: fix/w47-b2
commits: dbb4498b
tests: sreport_leave
signature:
docs: docs/REPORTING.md#reporting-session
related: BUG-0767
---

## Report
Suspected from reading the code (not measured). When the host's session report round ends, the whole session zip
is built in one call on the game's main thread: the host's own report (up to 64 MB) is read, and it plus every
guest report (up to 25 MB each) is written. On a slow disk or under Wine this could stop the host's game for
seconds; with several big reports long enough for guests to time out (15 s) or for the watchdog's hang report.

## Root cause
Inferred from the code (rel/0.7.12 7ec10ba3), confidence low-medium (real reports are 1-5 MB):
mods/coop/sreport.cpp:113-194 `write_session()` runs through `H->run_outside_frame` (main thread, outside Present):
`read_file(g_host_zip, host_data, 64u << 20)`, then `ZipOut::add` with `fwrite` for every entry and `fclose`,
`MoveFileExW`, all synchronous; no network update runs meanwhile.

## Fix
Measured first (lane 4, Wine, NVMe): with a 1.2 MB guest report the write took 0-3 ms (`coop.sreport status`
`last_ms=`), so with real reports the stall was small; it grows with the data (the host's own report up to 64 MB and
up to 25 MB per guest are read and written in one go) and the disk. The cheap fix was taken anyway: the file work
(`do_write`: read the host's report, write the zip, rename) runs on a pinned worker thread (`net::g_spawn_thread`, the
spawn_pinned of coop.cpp, so a hot reload does not unmap it), with no SporeKit call on it; the result (line, log text,
entries) is taken in `sreport::tick()` on the main thread. The round stays "writing" meanwhile (no second round).
Twin switch `coop.fix core.sreport_write_worker off` (outside the frame on the main thread). Test knob
`coop.sreport writedelay <ms>` (a slow disk: that long per MB written).

## Fix idea
Move the Round out and write the zip on a worker thread (pinned like `spawn_pinned`, coop.cpp), posting the result
line back to the main thread; or write it in pieces (a few MB per outside-frame task).

## Verification
`coop_sreport_leave.py` part E (`writedelay 3000`: a 1.2 MB zip takes 3 s to write; the test polls the host's
console every 0.1 s and takes its slowest answer):
* FAIL with `SPORE_FIX_OFF=core.sreport_write_worker`: lab 20261110-184040 (the slowest answer 3.05 s: the game stood
  still for the whole write).
* PASS: lab 20261110-183640 (slowest answer 0.29 s; that run had core.session_state_end off, part E does not depend on
  it), 20261110-184220 (two runs, every part).

## How to test
Lab: three guests with `coop.sreport test` producing ~24 MB valid zips (a new test mode `bigvalid`), host on a lane
with a throttled disk (or a test knob that sleeps per MB); assert the host's longest frame gap during the write is
under 1 s and no guest times out. FAILS before, PASSES after.

## History
- 2026-11-10: opened.
- 2026-11-10: measured (0-3 ms for 1.2 MB) and fixed on fix/w47-b2 (worker thread; test sreport_leave part E).
