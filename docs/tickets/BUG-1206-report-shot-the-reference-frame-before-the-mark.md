---
id: BUG-1206
title: report_shot: the reference frame before the mark is taken while the world still fades in
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
The 0.7.15 final check (lab 20261116-025154, lane 5): report_shot "FAIL its brightness 99.8 is like the game's frame
just before (61.8, `shot`)"; the test printed "the guest's frame before the mark: mean brightness 61.8 after 2 s".
Every other check passed (the mark's JPEG, its size, the archive, the host's shot).

## Root cause
Observed: the test takes full screenshots (`shot`) of the guest until one is brighter than 30 and keeps that one as the
reference; the mark's own screenshot (the JPEG) comes about 2 s later. In the failed run the first shots were black
(the reference came "after 2 s") and the one taken was 61.8 while the world still faded in; 2 s later the frame was
99.8. In the passing runs on lane 9 (labs 20261116-082959, 20261116-084124) the reference was 100.2 / 105.1 "after 0 s"
and the mark 100.8 / 105.3. Inferred: with A9's start-up waits (BUG-1112) the test sometimes begins while the guest's
world is still appearing, which the fixed 4 s settle used to hide.

## Fix
coop_report_shot.py takes as the reference a frame whose brightness held: two shots 1 s apart over 30 and within 8 of
each other (still at most 45 s). The comparison itself (the mark's JPEG within 30 of the reference) is unchanged.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
