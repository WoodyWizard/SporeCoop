---
id: BUG-0252
title: Tests: make test fails on machines without $USER or without sanitizer runtimes, with errors that do not say why
status: open
kind: test
stage: All
reported: 2026-10-28
source: GitHub #186 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
From GitHub issue #186: https://github.com/WoodyWizard/SporeCoop/issues/186 (the full analysis, links and the scenario are there).

**Summary**
Release 0.7.3.0 added a lot to `make test`: the lab self-test (BUG-0036 and others), the tool self-tests (BUG-0097 and others), ASan/UBSan builds of every unit test, a TSan test (BUG-0077) and a decoder fuzz test (BUG-0208). This made `make test` depend on the environment in three ways that the old `make test` did not have:
- `tools/tests/lane.py` reads `os.environ['USER']` at import, and `lab_selftest.py` now imports it (through twoinst). Without `$USER`, `make test` stops with `KeyError: 'USER'`.
- `tools/spore.sh` uses `$USER` (and `$HOME`) under `set -u`.
- Every unit test now links with sanitizers. With the default `HOSTCXX=clang++` and no compiler-rt runtimes installed, the link fails. With gcc 13, TSan aborts on kernels with `vm.mmap_rnd_bits=32`, the Ubuntu 24.04 defaul

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
