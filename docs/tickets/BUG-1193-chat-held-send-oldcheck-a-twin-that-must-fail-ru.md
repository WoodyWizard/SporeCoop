---
id: BUG-1193
title: chat_held_send_oldcheck, a twin that must FAIL, runs in the full suite and fails every run
status: open
kind: test
stage: Tools
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
