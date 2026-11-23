---
id: BUG-1533
title: lifecycle: the events kPlayerJoin / kPlayerLeave are declared and listed by coop.lifecycle but never fired (a LIFECYCLE_HOOK on them is never called)
status: open
kind: tooling
stage: All
reported: 2026-11-23
source: feat/w69-progress code read 2026-11-23
role: host,guest
versions: 0.7.16.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Found while writing own progress (feat/w69-progress, BUG-1469): a `LIFECYCLE_HOOK(..., lifecycle::kPlayerJoin |
lifecycle::kPlayerLeave, ...)` was never called. lifecycle.h declares the events (Ctx.player, event_slot, the
`coop.lifecycle` listing shows J / L), but only kSessionEnd (session_glue.cpp end_session) and kWorldChange (coop.cpp
world tick) are ever passed to `lifecycle::fire`. The join / leave work of every module is called by hand from
session_glue.cpp's `cb.player_join` / leave callbacks. Reproduce: grep `lifecycle::fire` (two callers) or register a
hook on kPlayerJoin and join a guest: its `calls=0` in `coop.lifecycle`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
Not fixed (not part of WP-P1): own_progress.cpp uses the hand-written calls (on_player_join / on_player_leave from
session_glue.cpp) like every other module. Either fire the two events from the join / leave callbacks or drop them from
lifecycle.h so nobody relies on them.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
