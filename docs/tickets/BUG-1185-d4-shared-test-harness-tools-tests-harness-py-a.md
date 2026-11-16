---
id: BUG-1185
title: D4: shared test harness (tools/tests/harness.py), a codemod of the copied helpers over the coop tests, run_all.sh removed
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-harness
commits: 0eb42086, 9c7aab93, 2134cab8, dd944dcb, 0e1995ce, a09480bd, 74a6e5c3, 56b478b7, 34438ac3, 2299d8de, 5c365e41, d49a3978
tests: tools/tests/harness_selftest.py (make test, unit-tools), tools/harness_codemod.py --self-test, tools/tests/test_lint.py (copy check)
signature:
docs: docs/06-testing.md#testing-harness
related: BUG-0437, BUG-1187
---

## Report
Plan 0.7.15 D4 (audit TST-5): the game tests copied their helpers (94 `def wait`, 49 `def sp`, 50 `def mode`, 37
`def num`, 23 `def faults`, ...) and each picked "a wild NPC" its own way; a fix to one copy (the first-listed-NPC
class BUG-0437) reached only that test. tools/tests/run_all.sh was a stale copy of the suite list, unused since the lab.

## Root cause
No shared module for test helpers next to twoinst.py (which holds start-up and Checks only): each new test copied the
helpers of the test it started from.

## Fix
No game code changed (no switch: tooling only).
* `tools/tests/harness.py`: wait_until / waiter / poll (a reason prints the time waited or the last value), console
  parsing (mode and its variants, sp, pool, num, pool_num, field, field_dotall, kv, faults, busy_*, cinematic, eax,
  frame, world_of, puppet), positions (avatar_pos, cell_pos, dist, dist_or_none), states (shared_money, shared_food,
  dna), logs (log_size, log_since), pct, fnv, NPCs (npc_list, own_species, pick_npc: wild = not the host's species,
  not its family, alive, mirrored), results (check_line, result_line, inconclusive for A15). twoinst.Checks prints
  through it and has `inconclusive(why)`.
* `tools/harness_codemod.py` (plan / apply / verify): run once over tools/tests/coop_*.py, committed by area
  (Creature 26, Tribe 23, Civ 12, Space 57, cross-stage 60, Cell 36 files): 214 files, 314 copies (89 of the 94
  `def wait`), 1539 lines fewer (the diff over the tests: 303 lines in, 1842 out). Odd variants stay.
* test_lint.py `copy`: a test that copies a harness helper again fails make test.
* run_all.sh removed (suite.py's docstring named it); lab_selftest's fake layout carries harness.py; labimpact knows
  harness_codemod.py as a lab tool (harness.py itself is an import of nearly every test: a change selects them all).
* fix/w51-nest's coop_nest_follow.py, coop_nest_evolve.py and coop_campaign.py (left to the end): no copies in their
  current versions on that branch either (plan over `git show fix/w51-nest:...`).

## Verification
no-fail-run: tooling refactor (the tests' checks, thresholds and timings are unchanged by construction, proven below);
the new lint's FAIL case is planted in harness_selftest.py.
- `tools/harness_codemod.py verify rel/0.7.15`: 214 changed files, 0 problems (each module's syntax tree is the old one
  minus the replaced copies and their stdlib imports plus the harness lines; the FAIL / RESULT / INCONCLUSIVE / SKIP
  texts the same). pyflakes over the changed files: no new warning beyond "imported but unused" of helpers that were
  defined but unused before; every changed module imports.
- `tools/tests/harness_selftest.py` (make test): the 33 COPIES texts against their harness functions on fake consoles
  (481 calls: same results, same commands asked), 150 wait forms against their waiter on a fake clock (same value,
  cond calls, sleeps); a mutated harness function (faults returning None, a longer timeout) fails both checks;
  test_lint flags a planted copy.
- Lab sample on lanes 4/5 (build of the branch before the merge of rel/0.7.15 57cff006): 20261115-225537 (12 tests:
  10 ok; tag_cinematic FAIL, its cutscene did not end in 40 s of Enter, ok on the rerun 20261115-230446;
  tribe_zero_fields FAIL), 20261115-230746 (tribe_intro_camera, tribe_dead_member ok). tribe_zero_fields fails the
  same with the test's rel/0.7.15 text on the same build (20261115-230558): a regression of rel/0.7.15, BUG-1187.
- After the merge of rel/0.7.15: smoke on lanes 4/5 20261115-231011 (13 of 14 ok; `unit` failed only on this
  ticket's half-written state, the tickets check) and the sample again (below).

## History
- 2026-11-15: opened.
