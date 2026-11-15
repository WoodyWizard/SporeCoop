---
id: BUG-1132
title: C5: Space kept state (colonies, cargo hold, badges, Rivals budget) through one KeptStore with one handshake and restore window
status: fixed
kind: tooling
stage: Space
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-space
commits: a52441a2, ffeaa831, b2b463c1, 97ac395f, 971d3322, 781e7848
tests: tests/space_tests.cpp (test_kept_store, test_kept_map, test_kept_rivals, test_restore_window), lab space_kept_key space_colony_rejoin space_colony_crash space_items_rejoin space_items_missed space_items_limits space_trade_restore space_rival_merge space_rival_away space_badges_rejoin
signature:
docs: docs/01-architecture.md#arch-space-kept-store, docs/06-testing.md#testing-wire-golden
related: BUG-1133, BUG-1107, BUG-1135, BUG-0195, BUG-0678, BUG-0786, BUG-0810, BUG-1029, BUG-1034
---

## Report
Plan 0.7.15 C5 (audit NET-1): a rejoining Space guest's own state is kept by the host and handed back by four
hand-built protocols (space_map K_KEPT/K_KEPT_DONE, space_trade K_INV/K_INV_KEPT/K_INV_DONE, space_badges
K_BADGES/K_BADGES_KEPT/K_BADGES_DONE, space_rivalry K_ASK/K_SET), each with its own table, serial, timeout, leave rule
and restore window; bug class L (state outliving its context), 37 tickets, most of 0.7.13-0.7.14.

## Root cause
The same state machine written four times (BUG-1041 was one of them missing in a list three times).

## Fix
`mods/coop/space_kept.h` (portable, unit tests first): `KeptStore<T>` (host: per player key, world clear, bind with the
first-report rule and the exclusive variant of space_map, begin / keep / done with serial, 10 s resend, leave,
session end, keyed find / put for Rivals), `RestoreWindow` (guest: join, fresh, applied, none, known budget,
open / pending of BUG-1034), `HeldQueue` (the BUG-1034 queue). The four features use it; their wire bytes are unchanged
(C6 structs and golden bytes, BUG-1133). Done behind `coop.fix space.kept_store` (lab run with it on), then the four hand
paths and the switch were deleted (no new switch remains). docs/01-architecture.md#arch-space-kept-store.

## Verification
- no-fail-run: a refactoring (no behaviour change, the plan's proofs instead of a twin). Unit: space_tests runs the
  store through join, rejoin, crash rejoin with the same id (and the code before BUG-0810, which fails), same-name keys
  (BUG-1107), another world, session end mid-restore, the 10 s resend, a restore's failed list, Rivals budgets, the
  BUG-1034 queue and its cap.
- The features' own switches still work on the store: SPORE_LAB_FIXOFF=space_badges_rejoin=space.kept_token PASS
  (20261115-025652, 20261115-030136, 20261115-032648).
- Lab (feat/w51-space, lanes 6/7): with the store behind `space.kept_store` on: 20261115-023819 (kept_key,
  colony_rejoin, colony_crash, items_rejoin, items_missed, items_limits, trade_restore, rival_merge, rival_away,
  rivalry PASS; badges_rejoin FAIL = BUG-1135, the same FAIL on rel/0.7.15 without C5; space_trade FAIL = BUG-1139,
  test order). After the old paths were deleted, twice: 20261115-030136 (19/21) and 20261115-032648 (20/21), each with
  the 10 kept tests, space_rivalry, space_store, space_rescue, space_terra_tool, space_contracts, space_diplomacy,
  space_gift_refund, space_allies, space_colony_spice, refactor_dump and space_badges_rejoin with
  SPORE_LAB_FIXOFF=space_badges_rejoin=space.kept_token (PASS both: the store restores; the key is BUG-1135); the
  failures: space_trade warm after rivalry (BUG-1139; cold PASS 20261115-025652, 20261115-034607, 20261115-034658),
  space_allies once with a caught game fault (BUG-1144; PASS in 20261115-032648 and on rel/0.7.15 20261115-032050).
  play_space --quick PASS: 20261115-032035, 20261115-034557.
- Refactoring proof: `tools/refactor_dump.py compare` of rel/0.7.15 d0cdc8ea (worktree feat/w51-space-base, run
  20261115-025652) against 20261115-030136 and 20261115-032648: coop.fix list (785), coop.lifecycle, players, guards,
  hooks, commands and callbacks identical on both games (the temporary switch is gone again); msgtable: the dispatch
  lines identical, only the count of the name `net::MSG_RES` grew by one (shared.cpp picks its decoder by type).
- make test: all parts pass.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-space (unreleased).
