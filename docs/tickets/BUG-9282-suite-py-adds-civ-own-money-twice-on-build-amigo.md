---
id: BUG-9282
title: suite.py adds civ_own_money twice on build/amigos
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-20
source: lane D integration QA (test/integration)
role: 
versions: 0.7.15.94
fixed_in: unreleased
branch: fix/suite-dup
commits: 9054e063
tests: unit-tools (tools_selftest.py testlint), test_lint.py
signature:
docs:
related: BUG-9259
---

## Report
tools/tests/suite.py on build/amigos (0.7.15.94) has the line `add('civ_own_money', ...)` twice (after `identity` and
again after `tribe_seats`). Seen in the integration QA of 2026-11-20 while merging feat/lineages-civ.

## Root cause
Observed: the merge of feat/lineages-civ into build/amigos kept both sides' line (feat/civ-own-money had added it
earlier, feat/lineages-civ carries the same line next to its own entries). Nothing checked that suite names are unique:
the lab keys result logs, flaky counts and verdicts by name, so a second entry runs the test twice and one result hides
the other.

## Fix
The second line removed. tools/tests/test_lint.py (make test) flags a suite name added twice ("the test name X is
added twice: one entry per name"); tools_selftest.py testlint checks the lint flags it. No switch: tooling.

## Verification
- FAIL without the fix: `python3 tools/tests/test_lint.py` on build/amigos db9b6ca9 with the new lint: "tools/tests/suite.py:
  the test name civ_own_money is added twice", 1 problem, rc 1.
- PASS with it: 0 problem(s); `tools_selftest.py testlint` 0 failure(s) ("a suite name added twice is flagged").
- no-fail-run: a tooling check, no game and no lab run; FAIL and PASS above are the lint's own runs.

## History
- 2026-11-20: opened.
- 2026-11-20: fixed on fix/suite-dup (from build/amigos): duplicate removed, test_lint flags duplicates.
