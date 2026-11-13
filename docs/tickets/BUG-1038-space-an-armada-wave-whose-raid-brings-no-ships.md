---
id: BUG-1038
title: Space: an armada wave whose raid brings no ships in 30 s resumes the same raid again instead of starting a new one
status: open
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-12
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-armada-empty-wave
related: BUG-0046, BUG-0227
---

## Report
Code audit for 0.7.14, finding SPC-12. Not seen at runtime, found by code reading.
An armada contract wave whose raid brings no ships in 30 s is logged "again", but unless that wave was itself a resumed one the next tick resumes the same raid (g_wave_mission still set) instead of starting a new one: no new raid ever starts and the wave waits 30 s after 30 s while the players stand on the colony.

## Root cause
Observed in code (main efd2e0ba), confidence medium (that the same raid stays empty is inferred):
* space_contracts.cpp:982-993: the 30 s branch clears g_wave_mission only when g_wave_resume_fix && g_wave_resumed.
* space_contracts.cpp:1034-1041: the next ready tick with g_wave_mission still in raids_on_target() resumes it (g_stable_since is old, g_next_wave not moved).
Correction of the audit's detail: the timeout does not increment g.wave (only the won branch at :1026 does); the bug is the resume of the same empty raid.

## Fix idea
In the 30 s branch, abort (SetState 7) and clear g_wave_mission in every case, not only after a resume, so the next ready tick calls start_raid. Switch: under the existing `armada_wave_mission` family, or `coop.fix space.armada_empty_wave off`.

## How to test
coop_space_armada_return.py has the hooks (g_hide_ships: the raid's ships never come): hide the ships for 35 s on a fresh (not resumed) wave, then show them; assert a new raid mission exists (coop.space contracts raids lists a mission that is not the old one) and the wave's ships come. Main: FAIL (the old raid is resumed).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
