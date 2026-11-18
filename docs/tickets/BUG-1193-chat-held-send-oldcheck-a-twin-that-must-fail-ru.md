---
id: BUG-1193
title: chat_held_send_oldcheck, a twin that must FAIL, runs in the full suite and fails every run
status: fixed
kind: test
stage: Tools
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 0.7.11
fixed_in: 0.7.15
branch: fix/w51-t1
commits: 72c59ae5, cc2ce72e
tests: chat_held_send_oldcheck, chat_held_send, labverdict_selftest (make test)
signature:
docs: docs/06-testing.md#testing-w51-t1-triage
related: BUG-0629
---

## Report
The 0.7.15 final check (lab 20261116-025154, full suite) failed `chat_held_send_oldcheck` (lane 4): "FAIL line 1 / line
2: the line sent during the held frames is seen on both games (0.0 s after the click)". The ledger: it failed every
run since it was added (5 of 5 since 2026-11-07, rel/0.7.11 included).

## Root cause
Observed: the entry is the FAIL twin of BUG-0629 (`SPORE_CHAT_WAIT=0`: the play bots' old check, one look 1.5 s after
the Send click, must miss the line sent during held frames); its FAIL is the proof that chat_held_send catches the bug.
suite.py gave it no tag, and `add()` makes such an entry a member of full and nightly, where every FAIL counts as a
failure. A twin that must FAIL does not belong in a suite whose verdict needs every test to PASS.

## Fix
suite.py: a `twin` tag in the "only when asked for" set and in SUITES (`lab.py run twin` runs the must-FAIL twins);
chat_held_send_oldcheck carries only it. The twin and its check are unchanged (it still must FAIL when run by name);
chat_held_send (the PASS half) stays in full.

## Verification
* FAIL: lab 20261116-025154 (in full); the twin itself still FAILs as it must, lab 20261116-082959 (rel/0.7.15, lane 9:
  "line 1 / line 2 ... seen on both games (0.0 s after the click)").
* PASS: labverdict_selftest `suite: ... the must-FAIL twin chat_held_send_oldcheck only in twin` (make test; FAIL against
  rel/0.7.15's suite.py).
* no-fail-run: a suite membership change (no game behaviour); proven by the unit check above.

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t1.
- 2026-11-18: released in 0.7.15.
