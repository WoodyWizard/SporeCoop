---
id: BUG-0147
title: from the second time a ship is disabled on, its repair starts with a 0.5 s head start (25 %)
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#126
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-space
commits: 30a25b0
tests: space_stale (coop_space_stale.py; --without-fix must fail)
signature:
docs: docs/re/space.md#space-stale-lookups, docs/06-testing.md#testing-space-stale
related: #126
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/126 (full text, discussion).

A disabled ship (the "ship disabled" rescue) is repaired when a partner stays within range for `kRepairHold` = 2.0 s. The time step in `down_tick` comes from a function-local `static double last`, which is updated only while the ship is disabled. When the ship is disabled again later (at least 60 s after the last time, because of the cooldown), the first step is `min(0.5, now - last)` = 0.5 s. A partner already close by gets 25 % of the repair in the first frame, so the repair takes 1.5 s instead of 2.0 s.

### Where
- `static double last = now;` and the clamped dt: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L202-L226
- Disabling resets `g_repair_t = 0` but not `last`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L145-L171
- `kDownTime = 20.0, kRepairHold = 2.0`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L32

### How to reproduce
Two instances in Space, in the same system:
1. On the guest: `coop.space goto` (next to the partner), then `coop.space hit 100000`. The ship is disabled. Run `coop.space hp` every frame (lab: `sk wait.frames 1` loop); `g_repair_t` rises from 0 in frame steps and the repair completes after 2.0 s.
2. Wait 61 s, then `coop.space goto` and `coop.space hit 100000` again.
3. The first `coop.space hp` after the hit shows a repair time of 0.50, and the repair completes after about 1.5 s.

### Expected
Every repair needs the full 2.0 s of proximity.

## Root cause
**What happens**

- First episode: `last` is initialized on the first `down_tick` call, so dt = 0 and the repair takes the full 2.0 s.
- The repair ends with `end_down`, and `down_tick` returns early while `!g_down`, so `last` keeps the time of the end of the first episode.
- Second episode, at least 60 s later (`now - g_last_down < 60` blocks earlier ones): the first tick computes `dt = min(0.5, now - last)` = 0.5, so `g_repair_t` = 0.5 out of 2.0 while a partner is within `g_repair_dist` (60 m).
- The repair finishes after 1.5 s of proximity.

If no partner is close, the same 0.5 is subtracted instead (`max(0, g_repair_t - dt)`), which has no effect.

### Confidence
Confirmed by reading the code. A second reviewer verified it. Severity low (a balance and consistency bug, no crash).

## Fix
Fixed in spacecoop.cpp: a new disable sets `g_down_fresh`; the first `down_tick` after it takes its
time step from now (0), not from the last disable's last frame. `coop.space repairfix 0` restores
the old head start.

## Verification
Done (2026-10-27, lane 6). `coop_space_stale.py` disables the guest next to the host twice (62 s
apart, the cooldown) and reads `repair=` 0.15 s after the second hit.
- FAIL on the build without the fix: lab run 20261027-183607 space_stale (and its retry): `repair=0.6` after
  0.15 s (first disable: 0.1); again with `--without-fix` in 20261027-190159.
- PASS with the fix: 20261027-190159.

The original plan:

`tools/tests/coop_space_repair.py` (two instances): disable the guest's ship twice, more than 60 s apart, with the host's ship next to it. Assert, on the guest's game state (`coop.space hp` ship state and HP), that each episode is repaired no earlier than 1.9 s after the disable. On main the second episode FAILS (about 1.5 s). With the fix both PASS (about 2.0 s).

### Related
#76 (ending the "ship disabled" state writes to the ship after Space was left, same feature), #41 (flying to a partner works while disabled), tracking issue #82.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #126.
- 2026-10-27: ticket opened from the issue.
- 2026-10-27: fixed on fix/w1-space (wave 1, Space).
- 2026-10-28: released in 0.7.3.0.
