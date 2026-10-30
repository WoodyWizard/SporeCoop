---
id: BUG-0320
title: Space finale: the host's entries_with stays 0 when it enters the core before the guest (test flake)
status: open
kind: bug
stage: Space
reported: 2026-10-30
source: lab 20261030-142729 space_finale, lane 9 (fix/w13-space)
role: host
versions: main 5d7f20e (0.7.3.1+)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
`coop_space_finale.py` (lab 20261030-142729, lane 9, GOG 2024): both games entered the galactic
core together and played SPG_CenterOfGalaxy, both got the Staff of Life, but the check "host:
entered with the partner (after waiting)" failed: the host's `coop.finale` showed `waits=1
entries=1 entries_with=0`, the guest's `entries_with=1`. What the players see is right; only the
mod's counter (and the test that reads it) is wrong.

## Root cause
Inferred from `space_finale.cpp`: while the host waits at the gate, the tick clears `g_waiting`
as soon as every partner is at the gate (`missing()` empty) and schedules the entry; `h_Enter`
then counts "with" only when `g_waiting` is still set or a partner's K_REACHED already arrived. When
the host enters before the guest's K_REACHED, the waited entry counts as alone. A race of the
order of the two entries, not of the change on fix/w13-space (finale code untouched).
Suggested fix: remember that the wait ended because everybody arrived (a flag set next to
`g_enter_at = now`) and count that as "with".

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
