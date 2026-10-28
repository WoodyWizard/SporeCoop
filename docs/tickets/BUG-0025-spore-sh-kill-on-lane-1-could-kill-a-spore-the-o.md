---
id: BUG-0025
title: spore.sh kill on lane 1 could kill a Spore the owner started
status: fixed
kind: tooling
stage: All
reported: 2026-10-25
source: code review (playbook comparison)
role: 
versions: 
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits: 14463ee
tests: tools/tests/tools_selftest.py kill (make test)
signature: 
docs: 
related:
---

## Report
kill uses pkill -f on the lane folder; on lane 1 that is the real install.

## Root cause
Not pid-scoped.

## Fix
`spore.sh kill` no longer uses `pkill -f` on the lane folder. `lane_procs` (an inline Python
helper) lists the lane's processes by command line: games (the Windows path of the lane's game
folder), launch sandboxes (bwrap) and wrappers (bottles-cli, start.exe). On lane 1 (the real
install) only games with a test argument (`-sporekit-profile:` or `-sporekit-instance:`, which
`spore.sh run` and every test pass) are ours; a lane 1 game without one is the owner's own Spore and
is never killed. While it runs, the Wine trees of the "Games" bottle are left alone too (the
bottle's wineserver may sit in a test's launch sandbox). The planned PID files were not needed: the
test argument identifies a test game on lane 1 for its whole life, also when a wrapper re-spawns it.
`spore.sh pids [inst]` prints what kill would end.

## Verification
`tools/tests/tools_selftest.py kill` (in `make test`): fake game processes (argv[0] = a Wine path) in a private PID
namespace (`unshare -Urpf --mount-proc`, so nothing outside the namespace is visible or killable),
an "owner" game on lane 1 without a test argument, a test host and guest.
- FAIL with main's spore.sh (`SELFTEST_SPORE=<main copy>`): "lane 1 kill leaves the owner's own Spore alone" FAIL.
- PASS with the fix (run several times).

## History
- 2026-10-25: reported (code review (playbook comparison)).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: fixed on fix/w4-tools (wave 4, tests and tooling).
- 2026-10-28: released in 0.7.3.0.
