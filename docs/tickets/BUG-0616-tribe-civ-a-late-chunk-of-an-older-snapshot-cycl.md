---
id: BUG-0616
title: Tribe / Civ: a late chunk of an older snapshot cycle restarted that cycle on the guest (newer chunks forgotten, older states over newer)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-06
source: BUG-0612 split (fix/w45-remote-bodies-2)
role: guest
versions: 0.7.8
fixed_in: unreleased
branch: fix/w45-remote-bodies-2
commits: db4b6090
tests: validate_tests (make test)
signature:
docs:
related: BUG-0612, BUG-0425
---

## Report
Part of the BUG-0425 umbrella (docs/generalisation-plan.md G8, "one CycleTracker for chunked snapshots"), split from BUG-0612. Tribe (tribe.cpp guest_state) and Civ (civ.cpp guest_state) send their snapshot in chunks of one cycle (u16 counter). The guest started a new cycle at ANY other cycle number: a late chunk of an OLDER cycle (a loss burst, reordering) cleared the chunks of the newer cycle being received, so neither completed (no cleanup that cycle), and the older chunk's states (positions, health) overwrote the newer ones already applied.

## Root cause
Observed in the code (tribe.cpp / civ.cpp `if (cycle != g_rx_cycle)`): no order between cycles; inferred effect on screen: a mirror stepped back to its older state for one cycle and the cycle's removal of gone objects was skipped. Not reproduced in the lab (netem does not reorder past a whole cycle often enough to see it).

## Fix
`net::CycleTracker` (net/validate.h): one tracker of the cycle being received and its chunk bits (256), `cycle_order` with the u16 wrap; a chunk of an older cycle (within half the range behind) is dropped and counted (`older_chunks=` in `coop.tribe` / `coop.civ`). Used by Tribe and Civ. Switch `net.cycle_order` (off: any other cycle restarts, 0.7.8).

## Verification
no-fail-run: unit test only (the lab's netem cannot make a whole-cycle reordering on demand). tests/validate_tests.cpp test_cycle: chunks 0, 1 of cycle 5, the late last chunk of cycle 4, then chunk 2 of cycle 5: FAIL with `validate_tests --nofix` (lines `!late`, `t.complete(3)`), PASS without. Regression: play_tribe, play_civ, chaos_tribe of this branch (see BUG-0612).

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-remote-bodies-2, M2): opened from BUG-0612 and fixed (net.cycle_order).
