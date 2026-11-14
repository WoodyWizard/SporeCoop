---
id: BUG-1103
title: Tests: puppet_walk dies with ZeroDivisionError when the partner's remote line has no state (pos 0 0 0) at a gopos
status: fixed
kind: test
stage: Creature
reported: 2026-11-14
source: final check 0.7.14, lab 20261114-005006 puppet_walk
role: guest
versions: rel/0.7.14 4dd8d2be
fixed_in: unreleased
branch: fix/w49-fcgates
commits: d88810ad
tests: puppet_walk
signature: coop_puppet_walk.py beside_pos ZeroDivisionError (up = [x / r for x in p])
docs: docs/06-testing.md#testing-fcgates-0714
related: BUG-0742, BUG-1093
---

## Report
The 0.7.14 final check (lab 20261114-005006, warm) failed puppet_walk with a Python traceback after the first direction's
checks had passed: `beside_pos` -> `ZeroDivisionError: division by zero` in the walker's "to the partner (gopos)" step;
the rerun (20261114-022615) passed.

## Root cause
Observed (code + log): when the walker has lost its partner twice it reads the partner's position from the
`remote N ... pos=(x y z)` line of `coop.status` and walks beside it. That line prints `state=no ... pos=(0.0 0.0 0.0)`
when the partner's state buffer has nothing to sample at that moment (a gap in the states under the final check's load,
6 lanes); the test took (0 0 0) as a position and divided by its length.

## Fix
coop_puppet_walk.py: the gopos step only with a position off the planet's centre (a state to sample); otherwise the walk
goes on and tries again at the next tether check.

## Verification
no-fail-run: test only (a traceback on a missing state; the walk's checks are unchanged). PASS: out/lab/20261114-033453.

## History
- 2026-11-14: opened and fixed on fix/w49-fcgates (final check 0.7.14 triage).
