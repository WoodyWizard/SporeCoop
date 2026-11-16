---
id: BUG-1162
title: C8: tribe.cpp split along the seams of C1-C4, state grouped into structs, debug() in its own file
status: fixed
kind: tooling
stage: Tribe
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-tribe
commits: 12ba3f2e, 1bade063, 87d96a1b, c3cb68f4, b7e3706c
tests: refactor_dump, make test
signature:
docs: docs/01-architecture.md#arch-tribe-files
related: BUG-1128, BUG-1140, BUG-1143, BUG-1160
---

## Report
Plan item C8 of docs/plan-0.7.15.md for Tribe (audit STG-7): tribe.cpp, 7.9k lines with 300+ file-scope globals, its
debug() of 1.4k lines in the shipped file; every wave touched it.

## Root cause
Structure: one hub file.

## Fix
1. State grouped into structs first (12ba3f2e, renames in code only): `HostTable g_host` (the C1 member table, next id,
   the dealing turn, split_for, born_for, bodies_sent), `GuestMirrors g_guest` (the C1 mirror registry, the C4 snapshot
   receiver, the tribe map, the mirror counters), `AnimStats g_anim` (the 33 counters of the C7 member kinds), the
   attack ring's anonymous struct named `Ring` (an unnamed type cannot be declared in a header).
2. Four pure moves, one file each (1bade063 tribe_debug.cpp: debug() and list(); 87d96a1b tribe_anims.cpp: the member
   animation relay, the air height, the holds, the theme and party music; c3cb68f4 tribe_host.cpp: owner claims, the
   even share, host_tick, the guests' commands; b7e3706c tribe_mirrors.cpp: spawn / remove members, guest_state, the
   knock-out, the driving, guest_tick). tribe.cpp 7.9k -> 4.5k lines. The blocks core2 (C2 / C3) edits stay in
   tribe.cpp (the pause net, on_member_anims, the held commands, tick / on_message / session / world).
   What a move changed besides moving (refactor_dump.py moves): `static` dropped where a name became shared and its
   declaration in mods/coop/tribe_internal.h (at its first declaration's place), the types / constants / templates /
   one macro the files share moved into that header, the includes. Made with a clang-AST tool (each top-level entity,
   its references, the file it lands in; docs/01-architecture.md#arch-tribe-files).

## Verification
- refactor_dump (docs/10-dev-pipeline.md#pipeline-refactor-proof): base e57d8f2d (before the grouping) run
  20261115-180754 (out/lab of the worktree tag/w51tribe-c8base), after b7e3706c run 20261115-180842: `compare` says
  identical for every section (coop.fix 583 lines, coop.lifecycle 23, players, guards, hooks 214 / 212, commands 188,
  the message table, the manifest, the session callbacks), RESULT: identical.
- Each of the five commits builds (`make -j8`); `make test` passes on b7e3706c.
- The Tribe tests on the split tree: the final run (below).
- no-fail-run: pure moves, no behaviour change.

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-tribe): grouping and four pure moves; dumps identical.
