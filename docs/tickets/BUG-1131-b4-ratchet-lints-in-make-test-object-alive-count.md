---
id: BUG-1131
title: B4: ratchet lints in make test: object_alive count, CALL_GUARD around o_* calls, lifecycle registration of on_* functions
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-guards
commits: 42c13712, ae215268
tests: tools/ratchet_lint.py --self-test (make test part unit-lint)
signature:
docs: docs/06-testing.md#testing-ratchet-lints
related: BUG-1047, BUG-1055, BUG-0426, BUG-0427, BUG-1134
---

## Report
Plan 0.7.15 B4 (audits STG-9, CORE-4, CORE-5, CORE-3): three classes keep growing because nothing stops a new
instance: `object_alive` on a kept pointer (reads freed memory, BUG-0842..0844), a re-entrancy flag around a game call
that is no CALL_GUARD (a caught fault leaves it set, BUG-1047), a module's session / world function that the
lifecycle registry does not know (BUG-1055, BUG-0427).

## Root cause
No check: the registries (LiveRef, call_guard.h, lifecycle.h) exist, but using them is up to each author.

## Fix
tools/ratchet_lint.py (make test, unit-lint) over every mods/coop/*.cpp and *.h (a glob: the coop.cpp split into
coop_cmds / dna / vote / puppets / session_glue changes nothing), against tools/ratchet_baseline.json, entries keyed
by namespace (not file):
* alive: `object_alive(` calls (not in comments / strings, not the declaration) may only go down. Baseline 149
  (tribe 39, npc 22, civ 14, game 12, worldobj 8, nest 7, spacecoop 6, player_rules 6, squadron 5, ...).
* guard: a namespace-scope flag named like a guard (`g_in_*`, `*_applying`, `*_depth`, `g_inside*` of the audit,
  `*_relaying`, `*_guard`, `*_bypass` of today's registered names; not thread_local) set while a function calls an
  `o_*` trampoline (between the set and the next assignment, or in the Hold's block) must be a CALL_GUARD of its file
  or say `// not a call guard: <reason>`. Baseline: `game::g_in_puppet_anim` (2 functions) and game.cpp's effect
  `game::g_in_relay` (2 functions): real misses of BUG-1047, ticket BUG-1134. 18 guards are registered today; about
  130 statistic counters incremented before trampoline calls are kept out by the name pattern.
* lifecycle: `on_session_*`, `on_world_change/_end/_reset/_start`, `on_player_join/leave`, `on_disconnect(ed)` must
  be named in a LIFECYCLE_STATE / SESSION_STATE / WORLD_STATE / LIFECYCLE_HOOK of its file or carry
  `// lifecycle-exempt: <reason>`. Baseline 51 (all of them: the registry has no observer slots yet, C2 adds them).
Below the baseline: passes and prints `lower it with python3 tools/ratchet_lint.py --lower` (writes the lower
counts, refused while anything is above). No coop.fix switch (tooling, no mod code changed).

## Verification
- PASS: `python3 tools/ratchet_lint.py` on feat/w51-guards: 0 problems at the baseline.
- FAIL as wanted: `--self-test` plants each violation in a temporary tree (a second object_alive call, a bare
  `g_applying = true; o_Apply();`, a `++g_hook_depth` around `o_Ev`, an unregistered `on_player_leave`): each fails,
  `--lower` refuses each, a count below the baseline passes with the hint and `--lower` writes it. In the real tree a
  planted mods/coop/zz_plant.cpp (object_alive call, unregistered `g_in_plant` around `o_Plant`, unregistered
  `zz::on_session_end`) gave 3 FAIL lines and exit 1.
- The coop.cpp split (feat/w51-split f34aaf22: coop_cmds, dna, vote, puppets, session_glue) checked with this
  baseline (`--root <that tree> --baseline tools/ratchet_baseline.json`): 0 problems, the same 149 / 4 / 51.
- no-fail-run: tooling (no game code); the FAIL side is the planted violations above.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-guards (42c13712, ae215268).
