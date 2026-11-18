---
id: BUG-1183
title: C8: cellsync.cpp split along its seams (counters grouped into a struct first) and its debug() into its own file; creations.cpp's debug() too
status: fixed
kind: tooling
stage: Cell
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-cellsplit
commits: 0668e144, 4b6d040e, adddaa5b, 23ea0af4
tests: refactor_dump, make test, cell_*, play_cell
signature:
docs: docs/01-architecture.md#arch-cell-files, docs/10-dev-pipeline.md#pipeline-split-ast, docs/10-dev-pipeline.md#pipeline-refactor-proof
related: BUG-1164, BUG-1128, BUG-1186
---

## Report
Plan item C8 of docs/plan-0.7.15.md for Cell (audit STG-7): cellsync.cpp (7.3k lines, about 600 file-scope names, the
biggest stage file left) is touched by most Cell branches; split it along its seams, the state grouped first, and its
debug() (1.7k lines) into its own file, by pure moves. The plan's "four debug() functions": cellsync.cpp, tribe.cpp
(feat/w51-tribe), civ.cpp (done, BUG-1164) and creations.cpp (410 lines; no other branch edits creations.cpp now, so it
moves here too).

## Root cause
Structure (one hub file).

## Fix
1. 0668e144 (a rename): the 165 uint32_t diagnostic counters into `struct Counters g_n` (`g_n.created`, ...);
   counters that steer logic stay; test_lint counts the same 774 mod counter keys.
2. 4b6d040e (pure moves): cellsync.cpp 7.3k -> 1.7k lines; cellsync_internal.h (includes, layout constants, F<>,
   Counters, the shared types and hook types, for_each_cell, the declarations), cellsync_combat.cpp,
   cellsync_physics.cpp, cellsync_tiles.cpp, cellsync_host.cpp, cellsync_guest.cpp, cellsync_debug.cpp (debug(),
   coop.pool). Besides the moves only `static` dropped with a declaration in the header, the default arguments of
   nearest_partner and own_probe kept on the prototype, the new files' heads (`tools/refactor_dump.py moves`). Every
   lifecycle registration and init() with the hooks stay in cellsync.cpp. tools/labimpact.py maps cellsync_* to Cell.
3. adddaa5b (pure move): creations.cpp's debug() into creations_debug.cpp, creations_internal.h; the object_alive
   ratchet's per-file count follows it (total 146).
The shared names came from clang's AST dump (docs/10-dev-pipeline.md#pipeline-split-ast).

## Verification
- refactor_dump (lane 6): base 7a733c60 out/lab/20261115-210149; after the counters 0668e144 out/lab/20261115-210903;
  after the split 4b6d040e out/lab/20261115-212006; after creations adddaa5b out/lab/20261115-212410:
  `tools/refactor_dump.py compare` identical in every section (fix, lifecycle, players, guards, hooks, commands of both
  games; msgtable, manifest, callbacks), each step against the one before and the last against the base.
- make -j8, make test (all parts passed) after each step.
- The Cell lab set on the split build adddaa5b (every cell_* of tools/tests/suite.py and play_cell --quick, lanes 6 / 7,
  load average 20-30 from other runs): out/lab/20261115-212528, 93 of 101 OK (play_cell OK). The 8 that failed ran
  again at the same time on the split build (lane 6, out/lab/20261115-224549) and on the base 7a733c60 (lane 7, its
  own tree, copied as out/lab/20261115-224549-base): cell_new_game, cell_part_token, cell_guest_hit OK on both;
  cell_kill_once OK on the split, FAIL on the base; cell_swallow_gone, cell_unknown_model, cell_editor_timed, cell_sap
  FAIL on both with the same check. So no failure comes from the split; the four are BUG-1186 (opened, rel/0.7.15).
- no-fail-run: pure moves and a rename (no behaviour to fail); the proof is the identical dumps above.

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-cellsplit): counters grouped, six Cell files and creations_debug.cpp split out by pure moves,
  dumps identical.
- 2026-11-18: released in 0.7.15.
