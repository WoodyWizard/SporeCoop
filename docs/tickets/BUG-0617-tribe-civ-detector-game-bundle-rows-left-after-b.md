---
id: BUG-0617
title: Tribe / Civ: detector game_bundle rows left after BUG-0548: carried food moves with its member (copies 1-3.5 m behind, compared at the static 3 m), a guest member's own carried food is not on the host, Civ guest-only bundles in other containers
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-06
source: fix/w45-world-objects play_tribe / play_civ SPORE_DETECTOR=20 (lab 20261106-051335, 20261106-051337)
role: 
versions: 
fixed_in: unreleased
branch: fix/w45-last
commits: ef1058d0, 20236abd, 3c7dd711, 1a891434, 4d2df7df, 564496c8
tests: host_bundle_carry (coop_world_host.py --case bundle_carry), play_tribe / play_civ with SPORE_DETECTOR=20, tools_selftest.py (worlddiff bundle groups)
signature:
docs: docs/re/tribe.md#tribe-carried-bundles, docs/06-testing.md#testing-w45-last
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

* fix/w45-last (L5 of 0.7.9), observed: the census now names each bundle's holder (`own=ground|carried|held`, the
  carrier's player in `owner=`) and the detector compares them in three groups (carried at 6 m). play_tribe with the
  detector (lab 20261106-132448, 20261106-133817): `game_bundle_carried` rows were **not** the moving tolerance: the
  unpaired carried bundles had no partner within 28-70 m; on the guest, carried bundles of the guest's own members
  (owner 1) the host had none of (guest_only), and host carriers (an NPC tribe's, the host's, the guest's) without a
  copy. The mechanism (observed in host_bundle_carry with the fix off, lab 20261106-141416 #1): the guest's copy of a
  carried bundle is carried by the guest's copy of the member, which lags; at the next snapshot it was more than 3 m
  (9.5-21.5 m) from the host's bundle, stayed unbound, and an unbound carried bundle was kept as the guest's own for
  good: after the host's member put its food down the guest's copy of the member still carried it (2 of 3 cases, 5
  bundles left), and with its hand full the next copy could not be made. The guest's members run no behaviour of their
  own (ai_skipped = all ticks), so no carried bundle of the guest's game is its own. `game_bundle_held` (the food mats'
  bundles: the tribes' food, compared as the state's food values): its own group, left as each game's own.

## Fix
`wobj.bundle_carried` (worldobj.cpp, BUG-0617): a carried bundle binds within 8 m (Kind::tol_for), and a guest's carried
bundle without a host partner is not kept (dropped like any own copy after 3 snapshots), and the copy looks for the
carrier's copy within 8 m (was 4 m; it lags). `coop.fix wobj.bundle_carried
off`: 0.7.9 L2. Detector: worlddiff groups bundles by holder (TOL_CARRIED 6 m for carried) and names the unpaired
carriers' players; census rows of bundles carry own= / owner=.
Earlier note: Not done. Next: tell carried bundles apart in the census (their container) and compare them at the moving tolerance;
send the guest's own carried food to the host (or let the host's copy of the member carry it).

## Verification
host_bundle_carry: FAIL with `SPORE_FIX_OFF=wobj.bundle_carried` out/lab/20261106-141416 (#1: 5 carried bundles left on the
guest after the host's member put its food down; #2 passed: its copies bound), PASS with the fix out/lab/20261106-141728
(2 of 2, 0 left). The first version of the test (members standing near the host) passed both ways (lab 20261106-140459,
-140844): the carrier must walk.
Detector over play_tribe (SPORE_DETECTOR=20, ~31 comparisons each): before (lab 20261106-132448) carried rows
unmatched 7 + guest_only 5; with the fix (lab 20261106-150329) unmatched 3 + guest_only 2, single samples. Not verified:
inferred that the rest are the snapshots' timing (an unbound copy lives up to 3 snapshots before it is dropped; a missing
copy is made 3 s after the host's bundle first comes). Civ (play_civ lab 20261106-142030): no bundle rows.

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-world-objects): opened.
- 2026-11-06 (fix/w45-last, L5 of 0.7.9): census holder groups in the detector; unbound carried copies kept for good found and fixed (wobj.bundle_carried); test host_bundle_carry.
