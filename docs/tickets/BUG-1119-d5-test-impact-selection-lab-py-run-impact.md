---
id: BUG-1119
title: D5: test impact selection, lab.py run impact
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-labverdict
commits: 0481ee6e, c22c393a
tests: smoke, tools/tests/labverdict_selftest.py (make test)
signature:
docs: docs/06-testing.md#testing-impact
related:
---

## Report
Plan 0.7.15 D5 (audit TST-7). The lab had no changed-files selection: a tests-only or a Tribe-only change ran the
same 23 test-hours of `full` as a change to coop.cpp.

## Root cause
Missing feature.

## Fix
tools/labimpact.py, `lab.py impact [--base <rev>] [--why]` (print) and `lab.py run impact [--base <rev>]` (run):
the change is `git diff <merge base of base and HEAD>` + uncommitted + untracked files (default base: the nearest of
main and the local rel/* branches). The map (documented in labimpact.py and docs/06-testing.md#testing-impact): docs
nothing; a changed suite entry that test; the lab's tools `unit`; a test script or helper the entries whose script
imports it (transitive import graph of tools/ and tools/tests/); a stage module (STAGE_FILES) that stage's `full`
tests plus the cross-stage ones (no stage known: 93 of 802); a .sym change the stages of the files that use the
names; the tests naming a fix switch or the stages handling a MSG_* on the changed C++ lines; anything else core: all
of `full`; always smoke + unit. The final check and the release rule stay full. switch: none (lab tooling).

## Verification
* FAIL (before): rel/0.7.15 has no selection (`lab.py run impact` is "unknown test: impact"; the unit case `impact`
  of labverdict_selftest.py FAILs there).
* PASS: lab 20261115-014727 (lanes 2 and 3, `lab.py run impact --base HEAD` with a docs-only change): 14 tests
  selected (smoke + unit), 12.6 min; 13 PASS, `unit` FAIL from an unrelated ticket check problem (BUG-1129). Unit case
  `impact`: docs only -> smoke + unit; a lab tool -> unit; a test script -> its entries; a test library -> its
  importers; tribe.cpp -> the 171 Tribe tests + cross-stage, no Cell test (273 of 802 full); a line with
  g_fix_build_money -> the tests that name civ.build_money; coop.cpp -> all of full; a changed suite entry -> it.
* Replay over the 83 merges v0.7.10..v0.7.14: 67 core (coop.cpp in 49) -> all of full; 6 stage-only -> median 274
  tests (37 % of the suite-minutes); 10 tests/tools/docs -> median 32 tests (3 %).
* Not verified: whether the impact set at merge time would have caught the final-check failures of 0.7.12-0.7.14
  (the audit's suggestion; the regressions' merges are not known per failure).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-labverdict (unreleased).
