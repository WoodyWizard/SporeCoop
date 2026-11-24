---
id: BUG-1502
title: Settings: the settings of one world can be written into another during an editor or a load (NET-8)
status: fixed
kind: bug
stage: All
reported: 2026-11-23
source: audit
role: host
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w80-net
commits: 4649f447
tests: tools/tests/coop_settings_editor_lock.py (settings_editor_lock, twin settings_editor_lock_off)
signature:
docs:
related: 
switch: settings.world_kept
---

## Report
External static audit of 0.7.15.5, item NET-8 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-net-core.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (settings.cpp world_key / tick / started): the open world was "this game plays a stage"
(`C.playing()`), so the host's editor or a load closed it: `started()` was false, `join_closed` (late join off) let a new
player in, `can_set` unlocked the Session settings, and a change made then set `g_pending`, which the next world opened took
and saved (by design for the menu, wrong when the host went from A's editor to the menu and loaded B). The settings file was
written in place with `_wfopen("wb")` (a crash mid-write: defaults).

## Fix
settings.h `Ctx::holds_world` (coop.cpp: the game mode is the editor, Edit or Load): while it holds, settings.cpp keeps
the open world open and `started()` true; only the menu closes it. The file is written to `.tmp` and moved over
(MoveFileEx REPLACE_EXISTING | WRITE_THROUGH). Status `kept_frames=`. Switch `settings.world_kept` (off: 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: settings_editor_lock, settings_editor_lock_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run settings_editor_lock --lane N --retry 0 --no-build` (late join off, the host in its editor: Carol is refused,
  `coop.settings set all.friendly_fire on` is refused; after the editor Bob comes back).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run settings_editor_lock_off --lane N --retry 0 --no-build` (Carol joins during the editor, the setting is taken).
* Regressions near it: settings_creature, one_creature, editor_now.
0.7.17 release lab: PASS: settings_editor_lock (lab 20261124-020506); FAIL (fix off): settings_editor_lock (lab 20261124-003319).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fix and tests on fix/w80-net (4649f447); waiting for the night lab run.
- 2026-11-24: released in 0.7.17.
