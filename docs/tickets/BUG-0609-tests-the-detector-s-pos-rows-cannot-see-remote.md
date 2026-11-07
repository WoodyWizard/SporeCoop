---
id: BUG-0609
title: Tests: the detector's pos rows cannot see remote bodies that trail (15 m for creatures, 6 m for others, no time base)
status: fixed
kind: test
stage: All
reported: 2026-11-06
source: fix/w45-remote-bodies (M1)
role: 
versions: 
fixed_in: 0.7.9
branch: fix/w45-remote-bodies-2
commits: 6cf7d39b
tests: any test with SPORE_DETECTOR=<s> (body_trail_creature, body_trail_tribe, play_*)
signature:
docs: docs/06-testing.md#testing-detector-trail
related: BUG-0425
---

## Report
M1 (BUG-0425) asked for the class's detector rows to be gone on every stage. The detector (worlddiff.diff) has a
`<stage>:pos:<kind>` row only for matched objects more than 15 m apart (creatures, TOL_MOVING) or 6 m (others), and
it compares two dumps read one after the other (no common time): a copy that trails its host body by 0.2 s (0.6-2 m
at walking speed, the class) never makes a row. In the 0.7.8 final runs (rel/0.7.8 out/lab 20261105-*, play_*) there is
no `pos` row at all; the class shows only in the monitor's `proxy_stuck` lines (4 in those runs) and in
coop_body_trail.py (tools/tests/body_trail.py), which compares both games' own positions at the same moment.

## Root cause
Observed in the tool (tools/tests/worlddiff.py): the detector's only position rows came from two census dumps read one after the other (no common time) with 15 m / 6 m match tolerances, so a copy that trails by 0.2 s (0.6-2 m) never made a row.

## Fix
A common time base: both games' `coop.bodyrec` rows (QueryPerformanceCounter, one clock for every game on one PC). With SPORE_DETECTOR the monitor starts `coop.bodyrec on` (no clearing of a test's own rows) on the host and the guests and every period reads the rows since the last one (`coop.bodyrec since <t> [n] [step]`, one frame per 50 ms), then `body_trail.detect_findings` (the same compare as the body_trail tests: the host's body interpolated at each guest row's time) gives `<stage>:trail:<kind>` (npc, member, vehicle, player, ship) when lag p50 > 0.14 s and dist p90 > 0.5 m, or one copy over 2 m off for 1.5 s. `SPORE_DETECTOR_TRAIL=0`: off. The median lag, not p90: the detector's own census dumps make both games hitch and spread the tail.

## Verification
FAIL (the class shows as rows; SPORE_DETECTOR=10 SPORE_FIX_OFF=drive.lead): out/lab/20261106-041119: `Tribe:trail:npc` (p90 1.17-1.23 m, lag 0.21-0.27 s) and `Tribe:trail:member` (p90 2.01-2.06 m) in every period of body_trail_tribe, `Creature:trail:npc` (p90 0.73-0.97 m) in body_trail_creature. PASS (drive.lead on): out/lab/20261106-041542 body_trail_creature no trail row; out/lab/20261106-042100 one Tribe member row at lag p50 0.132 s under the first 0.12 s limit (now 0.14 s; per-10 s windows of the off runs 0.15-0.23 s). Regression with SPORE_DETECTOR=20: see the play_* runs in the report.

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-remote-bodies-2, M2): detector trail rows from coop.bodyrec; FAIL 20261106-041119, PASS 20261106-041542 / -042100.
- 2026-11-07: released in 0.7.9.
