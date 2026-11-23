---
id: BUG-1406
title: tests: defer_identity INCONCLUSIVE: its reuse hunt (xf20) predates BUG-1099 (tries before the free, misses destroyed, backward-merged rounds tried in vain)
status: fixed
kind: test
stage: Creature
reported: 2026-11-21
source: final check 20261121-003202 (defer_identity)
role: guest
versions: 0.7.13, unreleased
fixed_in: unreleased
branch: fix/w62-flake-tcs
commits: 7ba78dc2, 84c8f9e6
tests: defer_identity
signature:
docs: docs/06-testing.md#testing-w62-flake-tcs
related: BUG-0825, BUG-1099
---

## Report
0.7.16 third final check (lab 20261121-003202, lane 4, warm): defer_identity INCONCLUSIVE ("no block reuse in the
time"), as in the first final check (20261120-041304, BUG-0825's history). History: 6 of 20 failed on rel/main, 9 of 44
on all branches since 2026-11-12, INCONCLUSIVE 5 (1 in a row); the FAILs (2026-11-12, "0 times") are the test's older
version, before a run without a reuse was made INCONCLUSIVE. Not a 0.7.16 or w61 regression (no w61 commit touches
game.cpp's deferred destroys, npc.cpp's hunt or the test).

## Root cause
* Observed (the test's lines `host: reuse target=... chunk hdr=... freed_at=...` of the 16 rounds of 20261121-003202):
  7 rounds `freed_at=-1` (the game never let go of the queued puppet's block in the 900-frame hunt); 4 rounds (1, 4, 7,
  8) `freed_at=900` in the after-round line, freed 1026-1295 frames after the destroy (freed_frame minus the hunt's
  start), just after the hunt ended; 4 rounds (6, 10, 12, 13) `hdr=000016A8`: the chunk before the target was free, so
  the freed block merges backward and no creation can start at its address (docs/re/creature.md#creature-reuse-merged,
  BUG-1099); 1 round (11) freed at try 35 and none of the next 20 creations (one a frame, each destroyed at once) took
  it.
* Cause (test): the hunt `xf20` (one creature a frame, misses destroyed at once, 20 tries after the free) predates
  BUG-1099's finding that, once the block is free, 4 creations a frame with the misses kept take it in 21 of 27 rounds
  (one a frame destroyed at once: 0 of 21 to 2 of 10). Before the free, creations destroyed at once are what lets the
  game let go of the queued puppet (docs/re/creature.md#creature-freed-in-cinematic: 0 of 17 freed with no creation,
  1 of 4 with the misses kept), so `w` (no creation before the free) is not a fit here. The game's allocator and the
  time it holds its last reference stay outside the mod.

## Fix
Tests: `coop.npc reusehunt` gets three flags (npc.cpp ReuseHunt, tests only): `B<N>` N creatures a tick once the
target's chunk is free, `K` the misses kept from the free on (destroyed at once before it), `M` a target whose chunk
merges backward ends the round at once. defer_identity hunts `xf40B4KM` over 1500 frames (was `xf20`, 900): one a frame
destroyed at once until the free, then 4 a frame kept, 40 tries; merged rounds cost no time. No change of the mod's
behaviour; npc.defer_identity is untouched. `SPORE_DEFER_FLAGS=xf20` with `--frames 900` is the old hunt.

## Verification
no-fail-run: test setup only (how often the scenario is reached); the fix it tests (BUG-0825) keeps its FAIL/PASS runs.
INCONCLUSIVE (old hunt): out/lab/20261121-003202, out/lab/20261120-041304.
Was waiting for the lab run:
* `SPORE_DETECTOR=20 python3 tools/lab.py run defer_identity --repeat 3 --until all --lane N --retry 0 --no-build`:
  PASS wanted (a reuse in its rounds; the creature at the freed block lives after the cinematic).
* Twin: `SPORE_FIX_OFF=npc.defer_identity ... run defer_identity --repeat 2 --until all` must FAIL (the creature at the
  freed block destroyed with the queue), as before.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): defer_identity PASS in 20261122-194210, 20261123-021900 (2 of 2) and 20261123-023643; the twin SPORE_FIX_OFF=npc.defer_identity FAIL in 20261123-022402 (#1: "destroyed with the queue: 1"); the twin's two INCONCLUSIVE runs in 20261122-192135 were the hunt's budget of BUG-1453. Closed.

## History
- 2026-11-21: opened; the hunt changed on fix/w62-flake-tcs (xf40B4KM, 1500 frames), waiting for the lab run.
- 2026-11-23: CORRECTION (BUG-1453): the late frees at 1026-1295 frames were the cinematic's end (5-8 frames after its Enter), not a time effect; the hunt is 300 frames with up to 60 rounds now.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
