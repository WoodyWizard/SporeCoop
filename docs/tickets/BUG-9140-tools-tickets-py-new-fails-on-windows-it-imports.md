---
id: BUG-9140
title: tools/tickets.py new fails on Windows: it imports fcntl
status: fixed
kind: tooling
stage: All
reported: 2026-11-12
source: phase4-tribe agent on the Windows PC (BUG-9135)
role: host
versions: 
fixed_in: unreleased
branch: feat/lineages-47
commits: a8186716
tests: tools/tickets.py (manual: `py tools/tickets.py new` on Windows)
signature:
docs:
related:
---

## Report
`py tools/tickets.py new "..."` on the Windows PC stops with `ModuleNotFoundError: No module named 'fcntl'`
(tickets.py cmd_new, `import fcntl`). The other subcommands (find, check, index) work.

## Root cause
cmd_new locks `<git common dir>/tickets.lock` with fcntl.flock so parallel worktrees never take the same id; fcntl
exists only on POSIX.

## Fix
tickets.py `lock_exclusive`: fcntl.flock where it exists, else msvcrt.locking (LK_LOCK, retried) on the same lock
file. Before: the phase4 helpers used a stand-in `fcntl` module on PYTHONPATH (BUG-9138, BUG-9139 and this ticket).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
no-fail-run: tooling; before the fix `py tools/tickets.py new` raised ModuleNotFoundError on Windows (the report);
after it `lock_exclusive` takes the lock on Windows (checked 2026-11-12 on the owner's PC).

## History
- 2026-11-12: opened.
- 2026-11-12: fixed on feat/lineages-47 (msvcrt fallback).
