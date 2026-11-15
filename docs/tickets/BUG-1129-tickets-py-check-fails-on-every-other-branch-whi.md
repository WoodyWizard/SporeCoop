---
id: BUG-1129
title: tickets.py check fails on every other branch while a private ticket lists commits of an unmerged branch
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-15
source: feat/w51-labverdict, lab 20261115-014727 unit
role: 
versions: 
fixed_in: unreleased
branch: rel/0.7.15
commits: cdb65d1c
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
out/tickets/ (private tickets) is one folder for every worktree, while `rule_commits` and `rule_tests` check against
the current branch: a private ticket fixed on one branch names commits and tests the other branches do not have.

## Fix
`run_checks` turns the `commits` and `tests` findings of a private ticket into notes; public tickets are checked as
before. Checked on a detached copy of feat/w51-split (without fix/w51-net): BUG-1108..1110 no longer fail it.

## Verification
FAIL: `tools/tickets.py check` on a detached worktree of feat/w51-split (d03e7301, without fix/w51-net): "BUG-1110:
commits: commit a74f8819 is not in this branch's history". PASS: the same tree with the fixed tickets.py: no BUG-1108..1110
line.

## History
- 2026-11-15: opened.
- 2026-11-15: after feat/w51-labverdict merged rel/0.7.15 (which has those commits) the check is clean there; branches made before that merge still see it.
- 2026-11-15: fixed on rel/0.7.15 (private findings are notes).
