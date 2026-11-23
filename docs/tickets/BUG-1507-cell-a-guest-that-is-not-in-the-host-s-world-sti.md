---
id: BUG-1507
title: Cell: a guest that is not in the host's world still sends its editor spend and its mate (CELL-4)
status: open
kind: bug
stage: Cell
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: feat/w81-cell
commits: 17bc2248, e89fe984
tests: cell_elsewhere_spend (coop_cell_elsewhere_spend.py); twin cell_elsewhere_spend_off
switch: cell.elsewhere_spend
signature:
docs: docs/re/cell.md#cell-audit-0717
related: 
---

## Report
External static audit of 0.7.15.5, item CELL-4 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-uifx-cell.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-L1 (feat/w81-cell). A behaviour change gets a switch and a twin.

## Root cause
Static reading (out/research/ext-audit-verify-uifx-cell.md, CELL-4), confirmed in the code of rel/0.7.17: spend_measure (cellsync.cpp) sent the own editor's spend whenever the mode was Cell / Editor, with no check of own_world::elsewhere() / host_left(); tick() ran mate_tick before the `is_guest() && !g_solo` guard, so a guest in its own Cell world (BUG-0797) still sent MSG_CELL_MATE poses. The host's spend_in and mate_in did not ask where the guest is; a spend that came while the host had no Cell game went into g_spend_owed, which only a session end or a leave cleared, and was paid into whatever Cell game the host entered next. In rel/0.7.16 own DNA (shared_dna 0) books a new guest's spend into its wallet (the host's count untouched by default); the mate ghost and shared DNA (all.shared_dna 1) remain.

## Fix
`cell.elsewhere_spend` (cellsync.cpp, cellsync_mate.cpp): a guest that does not play the host's Cell world (not_host_world(): elsewhere or host_left) sends no spend (logged, counted `held`) and no mate pose (a gone mate once, so the host drops its copy); the host refuses a spend and a mate pose from a guest that plays another world (own_world::peer_elsewhere: an older guest), counted `refused` / `mate_refused`; a spend owed carries the host's world and is dropped when the host leaves the Cell stage (no Cell game and no editor) or is back in Cell in another world (cell_logic::owed_drop, unit-tested). Counters: `coop.pool` line cell_audit. Test knob `coop.pool spend fake <n>` (as if the own editor had just spent n). Off: 0.7.16.1.

## Verification
Open, waiting for the lab run: pending night run: cell_elsewhere_spend (PASS expected: shared DNA, the guest in its own Cell world, a fake editor spend of 7 and its mating call: the host's S+0x2C unchanged for 10 s and no mate copy in the host's game), cell_elsewhere_spend_off (SPORE_FIX_OFF=cell.elsewhere_spend: FAIL expected). Unit test: tests/cell_tests.cpp audit_0717 (owed_drop).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-L1), branch feat/w81-cell.
- 2026-11-23: implemented on feat/w81-cell with its lab test and twin (switch cell.elsewhere_spend), waiting for the night run.
