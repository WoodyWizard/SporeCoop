---
id: BUG-0617
title: Tribe / Civ: detector game_bundle rows left after BUG-0548: carried food moves with its member (copies 1-3.5 m behind, compared at the static 3 m), a guest member's own carried food is not on the host, Civ guest-only bundles in other containers
status: open
kind: desync
stage: Tribe
reported: 2026-11-06
source: fix/w45-world-objects play_tribe / play_civ SPORE_DETECTOR=20 (lab 20261106-051335, 20261106-051337)
role: 
versions: 
fixed_in:
branch: fix/w45-world-objects
commits:
tests:
signature:
docs:
related: BUG-0548
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lab 20261106-051335, play_tribe): with BUG-0548's fix the counts match within one (5 / 5, 6 / 5, 4 / 5), but
1-4 bundles per comparison have no partner within 3 m. Carried food moves with its member at ~7 m/s and the guest's
copy of the member is 1-3.5 m behind (coop_world_host.py --case bundle), while worlddiff compares game_bundle as a
static object (3 m). Inferred: the guest's own members' carried food (kept by the guest, bundle_own) has no copy on the
host. Civ (lab 20261106-051337): 2 guest-only bundles at (320 369 -149), probably in containers the kind skips (a
leftover tribe mat's, BUG-0539).

## Fix
Not done. Next: tell carried bundles apart in the census (their container) and compare them at the moving tolerance;
send the guest's own carried food to the host (or let the host's copy of the member carry it).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-world-objects): opened.
