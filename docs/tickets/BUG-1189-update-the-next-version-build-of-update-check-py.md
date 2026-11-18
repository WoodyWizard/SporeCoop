---
id: BUG-1189
title: update: the next-version build of update_check.py fails, session_glue.cpp redefines COOP_VERSION (B2 split)
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 0.7.15 dev
fixed_in: 0.7.15
branch: fix/w51-t1
commits: b2f27580, 07113651
tests: update (tools/tests/update_check.py)
signature:
docs: docs/06-testing.md#testing-w51-t1-triage
related: BUG-1128, BUG-1214
---

## Report
The 0.7.15 final check (lab 20261116-025154, `update` on the free lane 6): `update_check.py` builds the next version of
the tree (`make B=build-v07142 coop CXX='clang++ -DCOOP_VERSION=\"0.7.14.2\"'`) for the local "GitHub" to offer, and
the build stopped: `mods/coop/session_glue.cpp:9:9: error: 'COOP_VERSION' macro redefined [-Werror,-Wmacro-redefined]`
(`make: *** [Makefile:70: build-v07142/obj/mods/coop/session_glue.o] Error 1`). Nothing of the setup / in-game update
path was checked.

## Root cause
Observed: B2 (BUG-1128, commit 07a2ed60, merge 447585d1 feat/w51-split) moved the session glue out of coop.cpp and
gave session_glue.cpp its own `#define COOP_VERSION SC_VER_STR` (from build/version.h, which the Makefile makes from
coop.cpp's text). coop.cpp guards its define with `#ifndef COOP_VERSION`; session_glue.cpp did not, so a
`-DCOOP_VERSION` on the command line (only update_check.py passes one) is a redefinition, an error under A4's
`-Werror`. The B2 Makefile rule (`$(B)/obj/mods/coop/session_glue.o: $(B)/version.h`) itself works with another
build dir (`B=build-v07142` made `build-v07142/version.h`).

## Fix
session_glue.cpp defines COOP_VERSION only when the command line did not (`#ifndef COOP_VERSION`, as coop.cpp), so
the fake build's text "coop 0.7.14.2, SporeKit ..." of the session's version line is the -D value too. A build-time
change only: the normal build (no -D) makes the same coop.dll text as before. No switch (no behaviour of the game).

## Verification
* FAIL: lab 20261116-025154 (`update`, the final check): `session_glue.cpp:9:9: error: 'COOP_VERSION' macro redefined`.
* PASS of the build: `make B=build-vtest coop CXX='... -DCOOP_VERSION=\"0.7.14.2\"'` with the fix: coop.dll holds
  "SporeCoop 0.7.14.2" and "coop 0.7.14.2, SporeKit" (the session's version line, now from the -D too).
* PASS of the whole test (with BUG-1214): `update_check.py --lane 9` by hand twice, RESULT: OK (out/manual/
  update_check-lane9-2.log, -3.log of fix/w51-t1: found 0.7.14.2, installed, 0.7.14.2 runs after the restart,
  uninstalled). The lab runs `update` only on a free lane outside its run; with lane 9 as the only lane of this task
  there is none, so the runs are by hand on lane 9.
* `make -j8`, `make test` pass.

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t1.
- 2026-11-18: released in 0.7.15.
