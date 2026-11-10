---
id: BUG-0780
title: 0.7.12-only: the session report zip is read and written in one call on the main thread (multi-second stall, suspected)
status: open
kind: freeze
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: 
versions: 0.7.12 (rel/0.7.12, unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- not fixed yet -->

## Fix idea
Move the Round out and write the zip on a worker thread (pinned like `spawn_pinned`, coop.cpp), posting the result
line back to the main thread; or write it in pieces (a few MB per outside-frame task).

## Verification
<!-- not fixed yet -->

## How to test
Lab: three guests with `coop.sreport test` producing ~24 MB valid zips (a new test mode `bigvalid`), host on a lane
with a throttled disk (or a test knob that sleeps per MB); assert the host's longest frame gap during the write is
under 1 s and no guest times out. FAILS before, PASSES after.

## History
- 2026-11-10: opened.
