---
id: BUG-1133
title: C6: Space and shared pool payloads as portable structs (decode, check, apply), fuzzed, golden bytes; libFuzzer in CI
status: fixed
kind: tooling
stage: Space
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-space
commits: a52441a2, 19f55ac8, db6744cd, a3ed71dc, b2b463c1, 97ac395f, 971d3322, 781e7848
tests: tests/wire_tests.cpp (59 shapes), tests/fuzz_decoders.cpp fuzz_wire, make fuzz, .github/workflows/fuzz.yml
signature:
docs: docs/04-netcode.md#netcode-wire-structs, docs/06-testing.md#testing-wire-golden
related: BUG-1132, BUG-0201, BUG-1110
---

## Report
Plan 0.7.15 C6 (audit NET-4, NET-10): about 170 hand reads of Space and shared-pool payloads sat next to game calls,
unreachable by the decoder fuzzer; checks applied unevenly (a cut K_KEPT_DONE list applied in part, `std::abs` of
-2^31 in K_MERGE_FIX); the libFuzzer build existed but nothing ran it.

## Root cause
No portable payload layer for the module messages (protocol.h had one only for the session's own types).

## Fix
mods/coop/wire/*.h: one struct per message kind for every MSG_SPACE kind (49 kinds, 1..87) and MSG_RES / MSG_RES_ADD;
handlers decode, check, apply (docs/04-netcode.md#netcode-wire-structs). No wire change: tests/wire_tests.cpp pins
59 shapes by hex and by the old writer calls copied verbatim. fuzz_decoders `fuzz_wire` fuzzes every struct (and
MSG_FIELDS); `make fuzz` is the libFuzzer build; `.github/workflows/fuzz.yml` runs it (2 min per push, 20 min weekly),
actions pinned by SHA (ci_lint clean). No switch (no behaviour change for valid input).
Behaviour changes, malformed input only: a K_KEPT_DONE cut inside its list is not used (the host keeps waiting and
resends K_KEPT after 10 s, before: partly applied and counted as answered); a K_MERGE_FIX of -2^31 is refused (before:
`std::abs` overflow let it through). Everything else refuses exactly what the old parse refused.

## Verification
- no-fail-run: no behaviour change for valid input; the wire is pinned instead: wire_tests compares every kind's bytes
  with a hex string and with the old writer calls copied verbatim (59 shapes ALL OK). Mutation check: with the bounds
  `n > 250` (K_INV items), `n > kMaxEntries` (badges) and `|d| < 1e8` (K_MERGE_FIX) removed, wire_tests FAILS 4 cases.
- fuzz_decoders (make test): every struct round trips, prefixes, 6000 mutations, ALL OK; `make fuzz FUZZ_SECONDS=30`
  (libFuzzer, clang 22): 1.36 M runs, no finding; ci_lint: 2 workflows, 0 problems. CI itself not run (no push).
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
