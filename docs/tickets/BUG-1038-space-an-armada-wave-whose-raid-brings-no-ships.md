---
id: BUG-1038
title: Space: an armada wave whose raid brings no ships in 30 s resumes the same raid again instead of starting a new one
status: not-reproduced
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-12
role: 
versions: 
fixed_in:
branch: fix/w49-space2
commits: daac928c, 8d73e8a9
tests: 
signature:
docs: docs/re/space.md#space-restore-window, docs/known-issues.md#issue-armada-empty-wave
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

Checked at runtime (fix/w49-space2): an empty raid does not stay in state 2/3 for 30 s in the game (see Verification).

## Fix idea
In the 30 s branch, abort (SetState 7) and clear g_wave_mission in every case, not only after a resume, so the next ready tick calls start_raid. Switch: under the existing `armada_wave_mission` family, or `coop.fix space.armada_empty_wave off`.

## How to test
coop_space_armada_return.py has the hooks (g_hide_ships: the raid's ships never come): hide the ships for 35 s on a fresh (not resumed) wave, then show them; assert a new raid mission exists (coop.space contracts raids lists a mission that is not the old one) and the wave's ships come. Main: FAIL (the old raid is resumed).

## Fix
Kept as hardening (no runtime effect seen): the 30 s branch aborts the empty raid (SetState 7) and clears the wave's
mission for a fresh wave too, so a raid still running after 30 s without ships is never resumed. Switch `coop.fix
space.armada_empty_wave off` (0.7.13).

## Verification
no-fail-run: not reproducible: the game never keeps an empty raid running for 30 s on the colony.
* out/lab/20261113-022716 (fix off, `contracts hideships 1` from the first wave: the mod saw no ships): the raid had
  left the game's running list (state 2/3) before the 30 s, so the next ready tick found nothing to resume and started
  a new raid (resumed_n 0); the same in out/lab/20261113-022655 with the fix.
* out/lab/20261113-023145 (fix off) and 20261113-023145-2 (fix on), raids started with 0 bombers (a test hook,
  removed again): the game completes such a raid at once (state 5), the wave counted as won.
Also: the audit's "every 30 s" was wrong: with contracts.wave_resume on, the resumed empty raid is aborted after its
own 30 s (BUG-0227), so at most one extra 30 s.

## History
- 2026-11-13: opened.
- 2026-11-13: closed not-reproduced on fix/w49-space2 (runs above); the abort kept as hardening behind space.armada_empty_wave.
