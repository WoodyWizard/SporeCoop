---
id: BUG-1116
title: A7: one flaky ledger over every results.json of all worktrees, fix-off twins excluded
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-labverdict
commits: 0481ee6e, c22c393a, 5a48f2dd, 91e5668c
tests: tools/tests/labverdict_selftest.py (make test), civ_money_short
signature:
docs: docs/06-testing.md#testing-lab-verdicts
related: BUG-0362
---

## Report
Plan 0.7.15 A7 (audit TST-2). The flake history was out/lab/flaky.json of the checkout a run started from (355
such files; main's covered 10 % of the results), and its `failed` counted the intended FAILs of fix-off twin runs:
results.json had no field for the switch. Agents investigated known flakes from zero.

## Root cause
lab.py wrote and read flaky.json per checkout (ROOT) and counted every non-ok result; nothing recorded a twin run.

## Fix
* results.json: `fix_off` (the SPORE_FIX_OFF of the test: the lab's environment or SPORE_LAB_FIXOFF for that test)
  and `twin` (planted old behaviour in the lab's environment: `*_OLD*`, `*PLANT*`, `*_TWIN`, `SPORE_*_FIX=0`,
  `*_GUEST_CMDS`; labverdict.TWIN_ENV); older results: the `SPORE_FIX_OFF: <x> off` lines of the log.
* `lab.py flaky [--days N] [--branch glob] [--test X] [--all] [--top N]`: one ledger over every results.json of the
  main checkout and all worktrees (tools/labverdict.py): fails / runs, rel/main apart, commits with both PASS and
  FAIL, INCONCLUSIVE and its streak, the twins and quick runs left out, the last reasons, the ticket candidates.
* Every run's summary.md (and its console end) lists each failed / flaky / INCONCLUSIVE test's history from the
  ledger ("known flake: 5 of 32 failed on rel/main, 12 of 72 on all branches ..."), replacing the per-checkout
  "Unstable tests"; flaky.json is still written.
* switch: none (lab tooling, no mod change).

## Verification
* FAIL (old): before, a fix-off twin's FAIL counted as a failure of the test and the history was per checkout
  (rel/0.7.15's summary of lab 20261115-012144 would have listed civ_money_short 3 failed); the unit case `ledger` of
  labverdict_selftest.py FAILs on rel/0.7.15 (`--tree`: no labverdict) and PASSes here.
* PASS: lab 20261115-012144 (`SPORE_LAB_FIXOFF="civ_money_short=civ.build_money"`, --repeat 6): every result has
  `fix_off: civ.build_money`, the ledger leaves them out; lab 20261115-012138 has `twin: ['CAMPAIGN_PLANT_STUCK=creature']`.
  Lab 20261115-012138's summary printed "campaign_ct: known flake: 5 of 32 failed on rel/main, 12 of 72 on all
  branches since 2026-10-31, mixed on 4 commit(s)".
* Against the audit's numbers (17 456 results, 2026-11-15): campaign 38 of 63 on rel/main, partner_pass 6 of 8,
  tribe_camera_null 5 of 7 (the same), 1873 twin results left out (the audit: 1901 by a looser match), `lab.py flaky`
  ~4 s.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-labverdict (unreleased).
- 2026-11-18: released in 0.7.15.
