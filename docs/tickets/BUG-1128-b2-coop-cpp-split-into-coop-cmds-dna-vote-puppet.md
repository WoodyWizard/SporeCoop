---
id: BUG-1128
title: B2: coop.cpp split into coop_cmds, dna, vote, puppets, session_glue by pure moves; sporekit_mod_main and the frame in named phases
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-split
commits: f37ab85f, f0031517, 728dc06c, 8397caab, 1f4594c4, b97ce418, 07a2ed60, 3fa72b95, a3b1d81c, 9681105a, d03e7301, 12bc9b41, b7eb4260
tests: refactor_dump (tools/refactor_dump.py, suite refactor), smoke on lane 10
signature:
docs: docs/01-architecture.md#arch-start-and-frame, docs/10-dev-pipeline.md#pipeline-refactor-proof
related: CORE-2 CORE-8 (docs/audit-0.7.15/core.md)
---

## Report
Plan 0.7.15 B2 (docs/plan-0.7.15.md, audit CORE-2 / CORE-8): mods/coop/coop.cpp was 8.6k lines with about 300 globals in
one anonymous namespace, touched by 42% of the bug tickets and the main merge-conflict point of parallel branches;
sporekit_mod_main (1.2k lines) and on_frame (200 lines, about 80 calls in an order bug fixes depend on) kept their
order only in comments.

## Root cause
Structure, not a bug: every concern of the core (commands, DNA routing, vote and own editor, puppets, session glue)
shared one file and its globals.

## Fix
No behaviour change, so no switch. Commits on feat/w51-split (each move one commit, nothing else in it besides
the declarations the move needs; `tools/refactor_dump.py moves <c>~1 <c>` lists those lines):
- f37ab85f tools/refactor_dump.py and the suite entry `refactor_dump` (suite `refactor`, only by name).
- f0031517 coop.cpp's anonymous namespace named `coop_core` (+ a using-directive in the new coop_state.h), H / M
  extern, LOG / SAY into the header. No code line changed.
- 728dc06c coop_cmds.cpp: every c_* command and the registrations of sporekit_mod_main (six runs, now
  register_commands_core / editor / civ / cell / stages / ui at the same places); coop.track / coop.bodyrec stay
  with their recorders. Types Remote, Stray, Vote, ReqImage, EditKind, CreatureGameMsg_t into the header.
- 8397caab dna.cpp: DNA totals' state, ledger notices, pace, on_dna, solo await, own spends.
- 1f4594c4 vote.cpp: votes, offers, own editor, editor hooks, game_busy / save_blocker, editor-pending flag.
- b97ce418 puppets.cpp: puppets, strays, local state, partner pass, animations, track / bodyrec / monitor / trace.
- 07a2ed60 session_glue.cpp: setup_callbacks, end_session, host / join / leave, roles, ready, the world share for
  guests (on_mode_change, follow_phase, share retries / waits, evolution in place, lobby). COOP_VERSION stays in
  coop.cpp (the tools read it); session_glue.cpp takes it from build/version.h (Makefile rule).
- 3fa72b95 coop_state.h: declarations grouped by their file (header only).
- a3b1d81c sporekit_mod_main: eight init phases, called in the same order.
- 9681105a on_frame: nine frame phases, called in the same order (CORE-8).
- d03e7301 docs (01-architecture, 10-dev-pipeline, CHANGELOG); 12bc9b41 merge of rel/0.7.15 with A4 / A11 replayed;
  b7eb4260 B5's triage_symbols_check accepts the phase inlined at sporekit_mod_main+0x40.
Non-move lines over the series: `static` dropped on 78 definitions now shared (and on 3 forward declarations), the
default arguments of host_elsewhere, parts_census, notice and chat_log kept only on their prototypes, the no-op
`(void)av;` at the end of its phase. state_fields_check.py names session_glue.cpp for PlayerState.species.
Not done here (behaviour changes, other packages): DNA / vote resets as SESSION_STATE entries (CORE-3, C2).

## Verification
no-fail-run: a refactoring without behaviour change; the proof is identical registries before and after.
refactor_dump on lane 10, `tools/refactor_dump.py compare <base> <run>` identical in every section (fix list,
lifecycle, players, guards, hooks, commands of host and guest; message table and callbacks from the source):
base rel/0.7.15 out/lab/20261115-011231; namespace 20261115-011340; coop_cmds 20261115-012416; dna
20261115-012622; vote 20261115-013051; puppets 20261115-013542; session_glue 20261115-014151; mod_main phases
20261115-014623; frame phases 20261115-014811. `make test` passed after every move commit.
Smoke on lane 10 of the split tip 9681105a (all of smoke but `unit`, run as make test, and `ui`, exclusive: it needs the
machine alone): 12/12 ok, out/lab/20261115-014927.
Merge of rel/0.7.15 (12bc9b41, 0c0f8c93: A11's coop.kick and A4's counter deletions replayed in the split files):
refactor_dump of the rel/0.7.15 tip dfdfb3d0 (out/lab/20261115-021145) and of the merged branch
(out/lab/20261115-021242) identical in every section (785 fixes, 184 commands with coop.kick); `make test` all parts
passed; lab `kick` ok (out/lab/20261115-021813).

## History
- 2026-11-15: opened.
- 2026-11-15: split and phases done on feat/w51-split (B2); rel/0.7.15 merged in, A4 / A11 replayed; fixed.
- 2026-11-18: released in 0.7.15.
