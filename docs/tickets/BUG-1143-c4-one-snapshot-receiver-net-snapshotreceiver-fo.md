---
id: BUG-1143
title: C4: one snapshot receiver (net::SnapshotReceiver) for the chunked snapshots of Tribe, Civ and the squadron
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-ccore, feat/w51-tribe, feat/w51-civ
commits: 423016ca, 99d04d57, 537387a8
tests: tests/core_tests.cpp
signature:
docs: docs/01-architecture.md#arch-snapshot-rx
related: BUG-0612, BUG-1051, BUG-0037, BUG-0685, BUG-1140
---

## Report
Plan item C4 of docs/plan-0.7.15.md (audit STG-2): the chunked receive of the host's snapshot cycles and the sweep after it are copied in tribe.cpp, civ.cpp (line for line) and squadron.cpp (its own mask, older window and done flag); each fix of the class (BUG-0612 cycle order, BUG-1051 reset, BUG-0685 / BUG-0951 sweep) was made two or three times.

## Root cause
Structure.

## Fix
Portable core (feat/w51-ccore): mods/coop/net/snapshot_rx.h, `net::SnapshotReceiver<Id>` with `net::SnapshotPolicy{max_chunks, keep_order, older_window, sweep_once, complete_on_last}` (Tribe / Civ: 255, net.cycle_order, 0x8000, last chunk; squadron: 31, 99, any chunk), `begin / saw / end / finish(registry, ...) / reset`, a generation per cycle, counters. Migration and switches (`tribe.snapshot_rx`, `civ.snapshot_rx`, `fleet.snapshot_rx`): docs/01-architecture.md#arch-snapshot-rx. Stays open until the three copies are replaced and deleted. Done: Tribe (feat/w51-tribe 0d049e6c, ba65cc5c), Civ and the squadron (feat/w51-civ 99d04d57, 537387a8): the copies, their state and the three switches are gone. The squadron's `coop.fleet chunkfix 0` (fleet.chunk, BUG-0037's test twin) keeps its own path: by the B1 rule it may not retire (a Space-file switch on the keep list, and no FAIL run id on record in BUG-0037).

## Verification
- no-fail-run: refactoring onto a shared receiver (the same behaviour, one documented difference: a duplicate of a completed cycle's last chunk does not sweep again); the PASS runs are above, the unit tests' mutations fail them.
Unit tests tests/core_tests.cpp (snapshot_tribe_civ: lost / cut / late older chunk, wrap, cycle_order off, reset after BUG-1051; snapshot_squadron; snapshot_handoff to the registry); mutations of the older-drop and sweep-once rules fail them. Lab proof with the adoption: Tribe on 20261115-162402 (on) and 20261115-175152 (off), see its History; Civ and the squadron: lab 20261115-161514 (84d96518, the switches on: play_civ, play_space, civ_owners_session, civ_city_ground, civ_capture_credit, civ_guest_attack, civ_aoe, civ_city_layout OK), 20261115-164954 (92ac086f, the old paths deleted: 10 OK), 20261115-174639 and 20261115-190127 (d1a4d5c5, the split build: 61 of the 62 others OK), 20261115-193058 (civ_save_strip OK), 20261115-193323 (play_civ, play_space --quick 2 of 2 each, civ_role_damage 2 of 2). Not by this branch: civ_role_damage's phase A (BUG-1172, fixed in the test), civ_owners_reload's sync-check gate (BUG-1175, FAILs on rel/0.7.15 9362bcdd too: out/lab/20261115-192303-base9362); space_fleet OK in 20261115-190127.

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-ccore): the portable core and its unit tests; no stage file changed. Open until the stage adoptions land.
- 2026-11-15 (feat/w51-tribe): Tribe adopted it (guest_state on net::SnapshotReceiver; g_cyc / g_cycle_ids /
  g_incomplete and the switch tribe.snapshot_rx deleted after the lab: on 20261115-162402 22/22 with tribe_cycle_restart
  and tribe_snapshot, off 20261115-175152 5/5). Commits 0d049e6c, ba65cc5c. Civ and the squadron are their branches'.
- 2026-11-15 (feat/w51-civ): Civ (`g_rx`, Tribe's policy, finish() into the civ.gv registry) and the squadron (`{31, true, 99, true, false}`) adopted it; the old paths and civ.snapshot_rx / fleet.snapshot_rx deleted after the lab. All three copies replaced: fixed.
