---
id: BUG-1504
title: Paint: a literal layer byte before an `FE N` run is patched and the run repeats the substitute's byte (CORE-8)
status: fixed
kind: bug
stage: All
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in: unreleased
branch: fix/w80-net
commits: 2adadbc9, ae038f10
tests: tests/validate_tests.cpp (test_model_paint_run_after)
signature:
docs:
related: 
switch: creations.paint_run_guard
---

## Report
External static audit of 0.7.15.5, item CORE-8 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-net-core.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (model_paint.h effects_read, creations.cpp the paint substitution): `patch[k]` was true when
layer k's four bytes were literal, also when the next token is `FE N` (N != 0), a run that repeats layer k's last byte;
rewriting the layer in place then made the run repeat the substitute's top byte over the next layers (wrong coat / detail
paint, maybe a broken model). The part reader has this guard (decode_run's `safe`). Needs an unknown paint to substitute and
a run right after it.

## Fix
model_paint.h effects_read(..., run_guard): a layer whose last byte is followed by `FE N` is not patchable (the
creation is refused as for a paint inside a run). creations.cpp passes the switch `creations.paint_run_guard` (off:
0.7.16.1).

## Verification
* no-fail-run: unit only: tests/validate_tests.cpp test_model_paint_run_after FAILS with run_guard false (0.7.16.1 reads
  the base layer as patchable) and PASSES with it; a creation with an unknown base paint whose top byte a run repeats cannot
  be made in a test game.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fixed on fix/w80-net (2adadbc9, ae038f10); closed with no-fail-run.
