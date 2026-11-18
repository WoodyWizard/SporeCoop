---
id: BUG-9139
title: win_tests: the founder-seat and party file tests never run in the full (lab) run
status: fixed
kind: test
stage: Civ
reported: 2026-11-12
source: found while adding a founders format test (roadmap phase 4, Civ helper)
role: host
versions: 0.7.8-lineages53-dev and later (since the BUG-9135 phase 1 commit a6ab1cf1)
fixed_in: unreleased
branch: phase4-civ
commits: 405644fb
tests: tests/win_tests.cpp (test_founders_multi, test_party)
signature: a full `win_tests.exe` run prints no founders_multi / party checks; only `--founders-only` runs them
docs:
related: BUG-9135
---

## Report
A test added to `test_founders_multi` (tests/win_tests.cpp) passed against the old founders.cpp in a full run.
`main()` calls `test_founders_multi()` and `test_party()` only inside the `--founders-only` branch (the indentation
there suggests otherwise); the full run, which the lab's `win_unit` test uses (`make test-win`, no arguments but
`--old*`), called only `test_founders()`. So the 8-seat DUO2 / CIV2 / OFT2 format checks of BUG-9135 phase 1 and the
`.duoparty` checks of roadmap 0b never ran in the lab.

## Root cause
Observed: phase 1 (a6ab1cf1) and 0b (e6b17ab7) added the calls next to `test_founders()` in the `--founders-only`
block only.

## Fix
The full run calls `test_founders_multi()` and `test_party()` after `test_founders()` as well.

## Verification
no-fail-run: test-only change, no lab here (Windows helper session). Built with the i686 llvm-mingw clang as the
Makefile does: the full run now prints the phase 4 founders checks; against the old founders.cpp (before the
phase 4 change of BUG-9135) the full run fails 3 founders checks (win_tests.cpp lines 420-422) besides the 4 known
installer checks that need SporeCoopSetup.exe; with the new founders.cpp only those 4 remain.

## History
- 2026-11-12: opened and fixed on phase4-civ.
