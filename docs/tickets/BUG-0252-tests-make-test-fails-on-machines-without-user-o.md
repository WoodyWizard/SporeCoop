---
id: BUG-0252
title: Tests: make test fails on machines without $USER or without sanitizer runtimes, with errors that do not say why
status: fixed
kind: test
stage: All
reported: 2026-10-28
source: GitHub #186 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w5-tools
commits: c06c869
tests: make test (tools_selftest.py noenv; env -u USER make test)
signature:
docs: docs/06-testing.md#testing-make-test-needs
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
Confirmed: lane.py read `os.environ['USER']` at import (lab_selftest imports it through twoinst), spore.sh used $USER / $HOME under `set -u`, and the sanitizer targets failed with the compiler's own link error when its runtimes are missing.

## Fix
lane.py `_user()` ($USER, else getpass, else "user"); spore.sh sets USER / HOME from `id -un` / getent when missing (not exported); Makefile target `sanitizer-probe` (tools/sanitizer_probe.sh, a stamp per HOSTCXX) builds and runs an ASan/UBSan and a TSan program before `test`, `test-asan`, `test-tsan` and prints what to install; TSan under `setarch -R` only when the plain run hits "unexpected memory mapping"; `SKIP_SANITIZERS=1` skips them loudly and is refused when CI is set.

## Verification
FAIL: `tools_selftest.py noenv` with SELFTEST_SPORE=<b423f56 spore.sh>: `USER: unbound variable` (line 81), `HOME: unbound variable` (env -i, line 23), lane.py KeyError. PASS: noenv 3/3 ok on fix/w5-tools; `env -u USER make test` exit 0 (whole suite); `make sanitizer-probe HOSTCXX=<a wrapper that fails every -fsanitize link>` prints the message and exits 1 before any test; `CI=true SKIP_SANITIZERS=1 make test` stops at once. Not run: a real Ubuntu clang without compiler-rt and the gcc 13 / mmap_rnd_bits=32 TSan case (the setarch path is untested here).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-tools (c06c869).
- 2026-10-29: released in 0.7.3.1.
