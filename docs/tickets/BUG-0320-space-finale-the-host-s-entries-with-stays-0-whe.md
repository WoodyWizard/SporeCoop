---
id: BUG-0320
title: Space finale: the host's entries_with stays 0 when it enters the core before the guest (test flake)
status: fixed
kind: bug
stage: Space
reported: 2026-10-30
source: lab 20261030-142729 space_finale, lane 9 (fix/w13-space)
role: host
versions: main 5d7f20e (0.7.3.1+)
fixed_in: 0.7.3.2
branch: fix/w14-space
commits: c89082a, 2752819
tests: space_finale_wait
signature:
docs: docs/06-testing.md#testing-space-mirror-model
related:
switch: space.finale_waited
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
`space_finale.cpp`: the tick sets `g_waited_all` when it ends the wait because every partner is
at the gate (not for "Enter now"); `h_Enter` counts that entry as together. Cleared with the wait,
at the entry and at the session end. Switch `coop.fix space.finale_waited`.

## Verification
Test `tools/tests/coop_space_finale_wait.py` (suite `space_finale_wait`): the host enters the core
and waits, the guest only flies to the gate (`finale goto core`, no entry), so the host enters
first; its `entries_with` must be 1.

* FAIL without the fix: lab 20261030-165154 (lane 8, `SPORE_FIX_OFF=space.finale_waited`):
  entries_with 0.
* PASS with the fix: labs 20261030-164615 and 20261030-165209 (lane 9): entries_with 1, the
  guest not entered, no faults.

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w14-space): fixed behind space.finale_waited; FAIL 20261030-165154, PASS 20261030-164615 + 20261030-165209.
- 2026-10-30: Space regression on lanes 8/9 with the fix, lab 20261030-170211: 15/15 (space_jump on lane 8 with the faulting host profile included).
- 2026-11-01: released in 0.7.3.2.
