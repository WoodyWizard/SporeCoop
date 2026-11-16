---
id: BUG-1143
title: C4: one snapshot receiver (net::SnapshotReceiver) for the chunked snapshots of Tribe, Civ and the squadron
status: open
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in:
branch: feat/w51-ccore
commits: 423016ca
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
Portable core (feat/w51-ccore): mods/coop/net/snapshot_rx.h, `net::SnapshotReceiver<Id>` with `net::SnapshotPolicy{max_chunks, keep_order, older_window, sweep_once, complete_on_last}` (Tribe / Civ: 255, net.cycle_order, 0x8000, last chunk; squadron: 31, 99, any chunk), `begin / saw / end / finish(registry, ...) / reset`, a generation per cycle, counters. Migration and switches (`tribe.snapshot_rx`, `civ.snapshot_rx`, `fleet.snapshot_rx`): docs/01-architecture.md#arch-snapshot-rx. Stays open until the three copies are replaced and deleted.

## Verification
Unit tests tests/core_tests.cpp (snapshot_tribe_civ: lost / cut / late older chunk, wrap, cycle_order off, reset after BUG-1051; snapshot_squadron; snapshot_handoff to the registry); mutations of the older-drop and sweep-once rules fail them. Lab proof with the adoption.

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-ccore): the portable core and its unit tests; no stage file changed. Open until the stage adoptions land.
- 2026-11-15 (feat/w51-tribe): Tribe adopted it (guest_state on net::SnapshotReceiver; g_cyc / g_cycle_ids /
  g_incomplete and the switch tribe.snapshot_rx deleted after the lab: on 20261115-162402 22/22 with tribe_cycle_restart
  and tribe_snapshot, off 20261115-175152 5/5). Commits 0d049e6c, ba65cc5c. Civ and the squadron are their branches'.
