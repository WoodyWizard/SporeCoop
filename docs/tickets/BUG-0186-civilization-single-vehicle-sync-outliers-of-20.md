---
id: BUG-0186
title: Civilization: single vehicle sync outliers of 20-27 m
status: fixed
kind: desync
stage: Civ
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.3.2
branch: fix/w13-tribe-civ, fix/w14-tribe-civ
commits: eb1bebf 4f73f10 72e32e6 0b6a85a
tests: civ_mirror_place (coop_civ_mirror_place.py), net_tests (test_velocity_jump_cap, test_velocity_hitch), play_civ
signature:
docs: docs/re/civ.md#civ-mirror-valid-ground, docs/re/civ.md#civ-vehicle-jump-velocity, docs/06-testing.md#testing-civ-mirror-place
related: BUG-0122, BUG-0188, BUG-0206
switch: civ.mirror_place civ.extrap civ.vel_cap
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* `play_civ.py` sessions: p90 of the vehicle position error 0.9-1.5 m, but the maximum of ~2500
  samples was 22-27 m in three sessions (one sample each). Not traced yet: candidates are a guest
  mirror bound to a nearby local vehicle of the same kind right after a purchase (the 25 m bind
  radius in guest_state) or the velocity estimate around a capture caption pause. Reproduce:
  `SPORE_LANE=N python3 tools/tests/play_civ.py --minutes 10 --seed 9` (it prints the outliers):
  `v13 22.7 m host (-497, 27, 66) guest (-493, 45, 79)` three samples in a row, an NPC vehicle on
  the far side of the planet; not seen on player vehicles.

## Root cause
Three causes, each seen at runtime:
1. Observed (runtime probe, fix/w14-tribe-civ): the recurring 13-17 m outlier (NPC vehicle #2 of
   nation 000021CB at its spice node near (-336, 132, 354) of civ_npc, every seed 9 / 11 run): the
   guest's game called SetPosition on the mirror every frame from Vehicle_KeepOnValidGround
   (0xCA7430, return 0xCA75B8), which moves a vehicle standing on ground its planet flags as
   invalid for its locomotion to the nearest valid spot (13 m away); guest_tick put it back each
   frame. The host's game keeps its vehicle there. CORRECTION of the w13 guess (an order of the
   guest's own game): the mirror had no order meanwhile, and clearing orders changed nothing.
2. Observed (fix/w13-tribe-civ): a one-step move of the host's vehicle became a 100+ u/s velocity
   and the mirror was put past it (err 27 m with a fresh state, no push).
3. Inferred from one sighting (age 0.4 s, 25 u/s air vehicle, err 19.6 m): extrapolation of up to
   0.4 s past the last state of a fast vehicle while the host hitched.

## Fix
* `civ.mirror_place` (0b6a85a/72e32e6): Vehicle_KeepOnValidGround is skipped for mirrors on a guest
  in Civ (`coop.civ` counter `mirror_place_skipped`).
* `civ.vel_cap` (eb1bebf): a step faster than 2 x the vehicle's top speed + 10 u/s is a jump.
* `civ.extrap` (4f73f10): a mirror is moved at most 0.15 s past its last state (was 0.4 s).
Tried and dropped (no effect on the push): clearing the guest's own orders on mirrors, snapping a
pushed mirror back (4f73f10 / e5d231d, removed in 72e32e6).

## Verification
* `coop_civ_mirror_place.py` (no command, the host's NPC vehicle claims the node by itself):
  FAIL with `SPORE_FIX_OFF=civ.mirror_place`: lab run 20261030-154424-fix-w14-tribe-civ (17.3 m,
  vehicle #2); PASS: lab run 20261030-154602-fix-w14-tribe-civ (3 runs, 1.2 / 1.0 / 0.8 m).
* Unit tests (net_tests): test_velocity_jump_cap (15.6 -> 0.0 units), test_velocity_hitch
  (10.0 -> 3.8 units, a 150 ms late packet 0.8 either way).
* `play_civ.py --minutes 10` on lane 7, vehicle sync max: before (w13/w14 baseline) seed 9 13.6-17.4 m,
  seed 11 14.9 m; with the branch seed 9 5.14 m, seed 11 1.52 m (out/w14/play_civ_fix_*.log of the
  worktree).

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28 (fix/w2-civ): not reproduced at 20-27 m: `play_civ.py --minutes 10 --seed 9` on lane 7 (GOG 2024, before the BUG-0188 fix and without the BUG-0122 fix) gave n=2430, p90 0.34 m, max 17.4 m (one sample, vehicle v2). Both candidates of the report are being removed by other fixes: the receive-time velocity of BUG-0122 (fix/w2-net: a late or bunched snapshot made the mirror overshoot by 22-40 units in the harness, which fits a single 20-27 m sample of a fast vehicle) and the guest's own NPC AI vehicles that could be bound as a mirror within 25 m (BUG-0188, fixed here; seen bound once in `coop_civ_ai_spawn.py --nofix`). Left open: re-run play_civ seed 9 after both branches are merged; close if the max stays under ~10 m. Most likely the same cause as BUG-0122 (inferred, not proven).
- 2026-10-28 (fix/w2-civ, after merging main with the BUG-0122 fix and the BUG-0188 fix): `play_civ.py --seed 9` again: n=2642, p90 0.15 m (was 0.34), max 17.4 m. The max is NOT jitter: vehicle v2 showed the same ~17 m offset at the same place in both runs (host (-334, 125, 359), guest (-347, 123, 347); the run before: host (-333, 123, 360), guest (-346, 122, 348)), two samples each, so the vehicle stood still with a constant offset. v8 had 11-12 m the same way. Not the same cause as BUG-0122. Next step: watch v2 in that run (`coop.civ` on both at 00:08 of the seed 9 session: purpose, order, a harbour or a city it stands in; the host position may be one the guest's game moves the vehicle out of, e.g. a building footprint). Logs: out/lab/w2civ-manual/play_civ.log, play_civ2.log (fix/w2-civ worktree).
- 2026-10-28 (fix/w3-civ): `play_civ.py --seed 9` on lane 7 again (with the wave 3 Civ fixes), and `play_civ.py` now prints the outlier vehicle's `coop.civ` line on both machines: n=1800, p90 0.19 m, max 14.8 m, again `v2` at the same place (host (-335, 129, 356), guest (-346, 129, 346)). v2 is an **NPC vehicle** (owner 254, pol 000021CB, purpose 0, order 8 on both). In the `coop.civ` dump taken right after the samples (a second or two later) the guest's mirror was 1.9 m from the host's (host (-335.1, 128.5, 356.5), guest (-336.4, 128.4, 355.2)): the offset is transient and converges, but it happens at the same place and time in every seed 9 run. Inferred, not proven: for a moment the guest's `guest_tick` does not pull the mirror (no state for that vehicle for more than 3 s, e.g. a chunk without it, or the vehicle just bound as a local one within the 25 m bind radius). Next step: log `now - g.seen` and the chunk of v2 on the guest at that time (00:08 of the session). Log: scratchpad of fix/w3-civ, play_civ9b.log (the run also showed 4 "vehicle owner 0 on the host, 1 on the guest" problems after gifts, see the log).
- 2026-10-28 (fix/w4-census): `play_civ.py --minutes 10 --seed 9` on lane 7 (main 7874654 + this branch): n=2509, median 0.00 m, p90 0.25 m, max 9.35 m, no outlier over 10 m (the v2 event of the earlier runs did not come: the bots' actions differ from run to run, 248 actions this time). New diagnostics for the next sighting: `coop.civ` on the guest now ends every mirrored vehicle's line with `age=` (s since its last state), `err=` (distance from where it should be), `pushed=` / `pushed5s=` (how far the game moved it away from where guest_tick put it one frame before, now and the largest in the last 5 s), and play_civ prints that line for every outlier; `coop.census` + `tools/tests/census.py diff` compare all vehicles by net id. pushed > 1 m on a standing vehicle would prove the inferred cause (the guest's game pushes the mirror out of something only it has there, e.g. a building); age > 3 s would prove the other one (no state). Left open: needs one sighting with these fields. Log: scratchpad fix_w4-census/play_civ9.log.
- 2026-10-30 (fix/w13-tribe-civ): `play_civ.py --minutes 10 --seed 9` on lane 7, before the cap: n=2352, p90 1.23 m, max 16.2 m: v7 and v10 (player vehicles) 16 m off at the same moment with `age=0.0`/`0.2`, `pushed` 0.0/0.3 and `err=27.0` on both: the target guest_tick wanted was 11+ m from the host's vehicle with a fresh state, so it came from the velocity estimate (observed); a step the host's game made by putting the vehicles somewhere (the bots had just given a defend order) became a speed (inferred). Fixed by civ.vel_cap (eb1bebf). With the cap (2 runs): run 1 n=2613, p90 0.49 m, max 14.6 m with two other causes, both `jumps=0`: v3 (NPC, owner 254) `pushed5s=13.1` and the guest's mirror had an order of its own (`order=8`, opos 0): the guest's game moved it (observed, why not known); v14 (NPC air, loco=1) `age=0.4` `err=19.6` `vel=25.2` `top=25.0`: a late state of a fast vehicle extrapolated while the host's vehicle turned (inferred). Logs: out/w13/play_civ9_a.log, play_civ9_fix1.log, play_civ9_fix2.log of the branch worktree (not in git). Stays open.
- 2026-10-30 (fix/w14-tribe-civ): the recurring outlier traced to Vehicle_KeepOnValidGround on the guest (runtime probe), fixed by civ.mirror_place; civ.extrap added; fixed.
- 2026-11-01: released in 0.7.3.2.
