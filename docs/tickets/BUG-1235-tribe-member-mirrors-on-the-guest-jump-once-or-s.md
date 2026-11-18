---
id: BUG-1235
title: Tribe: member mirrors on the guest jump once or stop-and-go 12-14% of frames at 30 fps over the player network (tribe_walk_player, old: 0.7.14.1 4 of 4)
status: open
kind: desync
stage: Tribe
reported: 2026-11-18
source: final check 20261117-154911
role: 
versions: 0.7.14.1, unreleased
fixed_in:
branch:
commits:
tests: tribe_walk_player
signature:
docs: docs/known-issues.md#issue-tribe-walk-player-jumps
related: BUG-0650, BUG-0651
---

## Report
0.7.15 final check (lab 20261117-154911): tribe_walk_player FAIL "guest sees its own members (mirrors): no jump of a
member while it walks (3)"; rerun 20261117-213354 "no stop-and-go while the members start and stop (uneven in 12% <=
10% of 183 frames at 1-5 m/s)". The test runs at 30 fps (`SPORE_FPS=30`) with the player network profile.

## Root cause
Not found yet. Observed: old, v0.7.14.1 fails 4 of 4 on lanes 6 / 7 (tag/v0.7.14.1-w51tribe out/lab/20261117-231627:
one jump of a member, or stop-and-go 13-14%), rel/0.7.15 3 of 4 (out/lab/20261117-223551, lanes 4 / 5: one jump, walk
gait 79% < 80%, at its speed 84% < 85%), the headless Xwayland saturated (90-92%) in two of them. History: 28 of 70
failed on all branches since 2026-11-01.

## Fix
Not fixed in 0.7.15 (an old desync at 30 fps, owner informed); next release.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened in the second 0.7.15 final-check triage; old (0.7.14.1 fails it too), left for the next release.
