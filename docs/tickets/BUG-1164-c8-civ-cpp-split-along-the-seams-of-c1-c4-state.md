---
id: BUG-1164
title: C8: civ.cpp split along the seams of C1-C4 (state grouped into structs first) and its debug() into its own file
status: fixed
kind: tooling
stage: Civ
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-civ
commits: 01f39d03, 92ac086f, f8be9a3d
tests: refactor_dump, make test
signature:
docs: docs/01-architecture.md#arch-civ-registry, docs/10-dev-pipeline.md#pipeline-refactor-proof
related: BUG-1140, BUG-1143, BUG-1163, BUG-1128
---

## Report
Plan item C8 of docs/plan-0.7.15.md for Civ (audit STG-7): civ.cpp (6.8k lines, 300 file-scope names) is touched by
most Civ branches; split it along the seams that C1, C4 and D3 leave, the state grouped first, and its debug() into its
own file, by pure moves.

## Root cause
Structure (one hub file).

## Fix
1. 01f39d03 (a rename): the 129 diagnostic counters into `struct Counters g_n` (`g_n.created`, ...); test_lint counts
   a key printed from `g_n.<name>` as a mod counter (the same 133 keys of civ.cpp as before).
2. 92ac086f and f8be9a3d (pure moves): civ.cpp 6.8k -> 2.7k lines; civ_internal.h (the shared includes, constants,
   types, templates and declarations), civ_tables.cpp (C1's registries and accessors, C4's receiver), civ_debug.cpp
   (debug()), civ_shots.cpp (the host's shots, BUG-0477 class; the old Tribe villages, BUG-0539), civ_guest.cpp (the
   guest's side, the vehicle mirrors and their snapshot), civ_host.cpp (claims, cities, host_tick, host_command).
   Besides the moves only `static` dropped with a declaration in civ_internal.h, default arguments kept on the
   prototype, the new files' head comments and includes (`tools/refactor_dump.py moves`). The lifecycle
   registrations and the hooks stay in civ.cpp. tools/labimpact.py maps civ_* to Civ; the object_alive ratchet's
   per-file counts follow the moves (total unchanged, 146).

## Verification
- refactor_dump (lane 6/7): base 537387a8 out/lab/20261115-164829-c8base (copied from its worktree run
  20261115-164829), after the first split 92ac086f out/lab/20261115-164829, after the second f8be9a3d
  out/lab/20261115-174353: `tools/refactor_dump.py compare` identical in every section (fix, lifecycle, players,
  guards, hooks, commands of both games; msgtable, manifest, callbacks).
- After the merge of rel/0.7.15 (5c3df1be): refactor_dump of the merged branch against rel/0.7.15 96e0fd4c itself
  (out/lab/20261115-200041-rel96e0, copied from its worktree run, vs out/lab/20261115-200041): identical in every section,
  so the whole branch (C1, C4, D3, C8) leaves every switch, lifecycle entry, guard, hook, command and message as it was.
- make -j8, make test (all parts passed); the Civ lab tests on the split build: see BUG-1140.
- no-fail-run: pure moves (no behaviour to fail); the proof is the identical dumps above.

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-civ): counters grouped, six files split out by pure moves, dumps identical.
