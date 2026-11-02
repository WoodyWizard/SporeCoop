---
id: BUG-0442
title: Report archives include a screenshot of the game at each problem mark
status: fixed
kind: feature
stage: any
reported: 2026-11-01
source: owner
role: both
versions: 
fixed_in: 0.7.3.4
branch: feat/w35-report-shot
commits: d4bf614, 47474c9, fa723b5
tests: report_shot (tools/tests/coop_report_shot.py), unit (tests/jpeg_tests.cpp, tools/tests/jpeg_check.py)
signature:
docs: docs/REPORTING.md#reporting-shots, docs/02-modkit.md#modkit-report-shots, docs/06-testing.md#testing-report-shots
related:
---

## Report
Owner's request (feature): when a player marks a problem (`/bug` in the co-op chat, `coop.bug`, F9
"Report a problem"), keep a screenshot of what that player saw at that moment and put it into the
report archive players send, so a "we see different things" report shows both screens.

## Root cause
Not a bug: the report archive had logs and state only; the devkit's `shot` (BUG-0065: refused on the
main thread) wrote full-size uncompressed PNGs to a Linux path for tests, nothing for players.

## Fix
SporeKit 0.3.6 `SkHost::report_shot` / `report_shot_pending` (sporekit/reportshot.cpp): the request
never waits; the next Present, after the overlay, StretchRects the back buffer on the GPU into a
1280-wide render target, reads that back once, and a worker thread writes a JPEG (own baseline
encoder, sporekit/jpeg.cpp, q80 down to q35 while over 450 KB) to mods\logs\shots (newest 5).
Archives add them under shots/, the report lists them under `== Screenshots` with their MARK line;
crash / hang / exit reports say they take none (D3D is not safe there). Fails soft: a log line.
Coop: every MARK (own and received) and the "Report a problem" button request one; the button
writes its archive once the shot is written (outside the frame, at most 4 s). Switch:
`[report] screenshot=0` in coop.ini (no shots, the kept ones removed).

## Verification
report_shot (lane 6, Wine/DXVK, windowed 1440x1080): FAIL with the feature off
(`SPORE_REPORT_SHOT=0`, [report] screenshot=0) in lab 20261101-183141 (no `report shot:` line, no
shots/ in either archive, no "screenshot included" status); PASS in 20261101-183022 (1280x960 JPEG
188 KB, mean brightness 108.2 against 107.8 of a full `shot`, 19.1 ms on the frame, no hitch; the
button's archive has shots/report-*.jpg). The first runs 20261101-182627 (a test regex bug) and
20261101-182830 (the mark's shot black: the warm guest's screen before the world shows; the test
now waits for it) are the test's own.
Final code (the shot line's caption "the mark by ..." instead of "MARK by ...": diag_creature
20261101-190234 FAILED on it, merge_logs and coop_diag.py count "MARK by" lines as marks): FAIL off
in 20261101-191135, PASS with report_shot, diag_cell, diag_creature in 20261101-190909.
Regression: smoke on lane 6 in 20261101-183818: 12 OK, `ui` skipped (exclusive, needs lanes 1-5),
`tribe_coop` FAIL "the guest box-selects its member" (seen the same on feat/w33-tests-session
20261101-180254, no report shot in it; OK on a rerun in 20261101-190234 with `diag`).

## History
- 2026-11-01: opened.
- 2026-11-02: released in 0.7.3.4.
