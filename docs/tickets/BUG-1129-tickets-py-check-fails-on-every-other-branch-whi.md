---
id: BUG-1129
title: tickets.py check fails on every other branch while a private ticket lists commits of an unmerged branch
status: open
kind: tooling
stage: Tools
reported: 2026-11-15
source: feat/w51-labverdict, lab 20261115-014727 unit
role: 
versions: 
fixed_in:
branch:
commits:
tests: make test (unit-lint)
signature:
docs:
related: BUG-0215
---

## Report
`make test` (part unit-lint, `tools/tickets.py check`) failed on feat/w51-labverdict (lab 20261115-014727, test
`unit`, lane 3; also by hand) with problems in tickets that are not on this branch: three private tickets (the main
checkout's out/tickets/, shared by every worktree) list in `commits:` commits of another 0.7.15 branch that is not
merged yet ("commit ... is not in this branch's history"), and their `tests:` name unit test functions of that branch
("names no lab suite, file or make test"). Every other branch of the batch fails `make test` the same way until that
branch is merged.

Reproduce: on any branch of rel/0.7.15 other than the one that fixed them, `python3 tools/tickets.py check`.

## Root cause
Inferred: tickets.py check applies the per-branch rules (commits in this branch's history, tests that exist in this
tree) also to the private tickets, which are not per branch: out/tickets/ is one folder for every worktree.

## Fix
Not fixed here (not part of A6/A7/A8/A15/D5). Suggested: for a private ticket, a commit outside the branch's history
and a test name the tree does not know are notes, not problems (or checked only on the branch named in `branch:`).

## Verification
<!-- the test: FAIL without the fix, PASS with it -->

## History
- 2026-11-15: opened.
- 2026-11-15: after feat/w51-labverdict merged rel/0.7.15 (which has those commits) the check is clean there; branches made before that merge still see it.
