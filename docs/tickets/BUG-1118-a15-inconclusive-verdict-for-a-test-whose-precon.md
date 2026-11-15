---
id: BUG-1118
title: A15: INCONCLUSIVE verdict for a test whose precondition did not happen
status: fixed
kind: test
stage: Tools
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-labverdict
commits: 0481ee6e, c22c393a
tests: content_civ, tools/tests/labverdict_selftest.py (make test)
signature:
docs: docs/06-testing.md#testing-lab-verdicts
related: BUG-0100, BUG-1116
---

## Report
Plan 0.7.15 A15 (owner decision 11, audit TST-10 and its open question 2). A test whose precondition did not happen
printed `RESULT: SKIP` (56 places): shown as a skip, never failed, so a test that never reaches its scenario looked
fine forever; and `lab.py fixcheck` counted a fix-off run that only skipped as the FAIL half of its twin.

## Root cause
lab.py had only ok / skipped; labcov.fix_off_runs took `ok and not skipped` as "failed", so a SKIP counted as the
twin's FAIL (lab 20261113-022622 cell_chunk_killer, `SPORE_FIX_OFF: cell.chunk_killer off`, "RESULT: SKIP (the kill
set no flee / chase target of its kind)", was one of the two FAIL runs confirming that claim).

## Fix
* Verdict INCONCLUSIVE (tools/labverdict.py): `RESULT: INCONCLUSIVE <why>` or the older `RESULT: SKIP <why>` (kept
  working, mapped) without a RESULT: FAIL; results.json `verdict`, `inconclusive`, `why` (ok / skipped kept for the
  older readers). Never a pass: console `INC` with the reason "(not a pass)", summary.md `INCONCLUSIVE`, the count
  "N INCONCLUSIVE (not passed)".
* Several in a row is a failure: the third INCONCLUSIVE in a row (the A7 ledger over every worktree, twins and quick
  runs left out) fails the test with `FAIL lab: INCONCLUSIVE n runs in a row (<why>): the precondition never happens;
  ticket candidate`; summary.md and `lab.py flaky` list the ticket candidates.
* Neither half of a twin: labcov.fix_off_runs leaves out every INCONCLUSIVE result (also a streak FAIL). The release
  rule lists INCONCLUSIVE apart and fails on the streak FAIL. No gate is loosened: a single INCONCLUSIVE is treated as
  SKIP was; everything else only gets stricter.
* switch: none (lab tooling, no mod change).

## Verification
* FAIL (old): rel/0.7.15's `lab.py fixcheck`: cell_chunk_killer / cell.chunk_killer "confirmed" by 2 FAIL runs
  (20261113-022622, 20261113-023438); one of them was the SKIP. PASS (new): the same claim with 1 FAIL run
  (20261113-023438, a real FAIL); mirror_fx_keys / mirror.fx_keys 3 FAIL runs -> 1 (20261113-014333 and -014513
  were SKIPs); no other claim changed (501 claims: 482 confirmed, 4 contradicted, 15 unverified, the same totals).
* Lab 20261115-013446 (content_civ with no free lane, lane 3): `INC content_civ ... INCONCLUSIVE: no second lane free
  for the guest without the pack (not a pass)`, "PASSED: 0/1, 1 INCONCLUSIVE (not passed)"; lab 20261115-014427 the
  same test with lane 2 free: PASS (the streak back to 0).
* Unit: labverdict_selftest.py `verdict`, `ledger` (streak, ticket candidate, fixcheck: an INCONCLUSIVE fix-off run is
  "unverified", not "confirmed"; release rule), `lab_run` (INCONCLUSIVE twice stays INCONCLUSIVE, the third in a row
  FAILs as a ticket candidate). lab_selftest.py `skipper` updated to the INCONCLUSIVE label.
* Today (2026-11-15) no test is INCONCLUSIVE 3 times in a row over all branches, so the rule fails nothing that
  passed before; on rel/* alone content_civ is.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-labverdict (unreleased).
