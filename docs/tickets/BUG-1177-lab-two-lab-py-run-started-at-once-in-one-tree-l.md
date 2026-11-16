---
id: BUG-1177
title: lab: two lab.py run started at once in one tree (lanes 9 and 10) both reported out/lab/20261115-193631; only one results.json (lane 9) is left there
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-15
source: fix/w51-evolve
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-labstate
commits:
tests: lab_selftest two_trees_case
signature:
docs: docs/06-testing.md#testing-warm-fingerprint
related: BUG-0353, BUG-0362
---

## Report
2026-11-15 19:36:31, tree fix/w51-evolve: `(SPORE_LAB_FIXOFF=... lab.py run evolve_world --lane 9 ...) & (lab.py run
evolve_world --lane 10 ...)` from one shell, the same second. Both printed `FAILED: 0/1 ... -> out/lab/20261115-193631/summary.md`
(lane 10: "FAIL evolve_world lane 10 222 s"; lane 9: 4.6 min); the directory holds one evolve_world.log, the two game
logs and a results.json with lane 9 only: the lane 10 run's results and logs are lost. new_run_dir (BUG-0353 /
BUG-0362) takes the id with os.mkdir and a -2 suffix, and an earlier pair started 1 s apart (20261115-193249,
20261115-193249-2) did get two directories.

## Root cause
Not looked into (not part of fix/w51-evolve). Reproduce: two runs of one test in one tree started in the same
second on two lanes. Workaround: start parallel runs a few seconds apart (docs/06-testing.md#testing-evolve-own-creature).

CORRECTION (2026-11-15, feat/w51-labstate): observed, the two runs were not in one tree. `rel/0.7.15/out/lab/20261115-193631`
holds the lane 10 run (results.json: tree SporeCoop-wt/rel/0.7.15, branch rel/0.7.15, head 96e0fd4c, evolve_world FAIL
lane 10 222 s), `fix/w51-evolve/out/lab/20261115-193631` the lane 9 run; nothing was lost. The second `lab.py run` of
the shell line ran in the agent's start directory (the rel/0.7.15 worktree), so it also tested rel/0.7.15, not the
fix. Inferred: the same-second id is unique per tree (BUG-0353 / BUG-0362: os.mkdir with -2 suffixes, lab_selftest
same_second) but not across the worktrees of the repository, and the last line printed only `out/lab/<id>/summary.md`
(no tree), so the two reports looked like one run.

## Fix
tools/lab.py `new_run_dir`: the id is taken in the repository as an O_EXCL file in `<git common dir>/lab-run-ids/`
(`run_id_registry`) before the tree's directory, with the same `-2`, `-3` suffixes: two runs in one second from two
worktrees get two ids. The start line names the run's directory and the last line the full path of its summary.md;
results.json is written to a temporary file of the run and renamed. `SPORE_LAB_RUNID_TREE_ONLY=1`: the old ids (unique
in one tree only), the self-test's FAIL case. docs/06-testing.md#testing-warm-fingerprint.

## Verification
no-fail-run: lab tooling; the proof is the unit case tools/tests/lab_selftest.py `two_trees_case` (in `make test`,
unit-lab): two fake lab runs started in one second from a repository and its worktree get 20260102-030405 and
20260102-030405-2, each tree's results.json holds its own result, the last line names the full path; with
SPORE_LAB_RUNID_TREE_ONLY=1 both get 20260102-030405 and the case FAILs (checked). same_second_case still passes. Real
runs since: out/lab/20261115-202550 (lane 9) and 20261115-202555 (lane 10), started 5 s apart, print their full paths.

## History
- 2026-11-15: opened.
- 2026-11-15: feat/w51-labstate: the runs were in two trees (CORRECTION above); run ids unique in the repository, full paths printed.
