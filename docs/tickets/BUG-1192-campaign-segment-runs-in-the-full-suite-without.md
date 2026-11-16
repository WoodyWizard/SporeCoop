---
id: BUG-1192
title: campaign_segment runs in the full suite without a segment and fails
status: fixed
kind: test
stage: Tools
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 0.7.15 dev
fixed_in: unreleased
branch: fix/w51-t1
commits: 72c59ae5, cc2ce72e
tests: campaign_segment, labverdict_selftest (make test)
signature:
docs: docs/06-testing.md#testing-w51-t1-triage
related: BUG-1115, BUG-1138
---

## Report
The 0.7.15 final check (lab 20261116-025154, full suite) ran `campaign_segment` (lane 4, 1 s): `RESULT: FAIL (no
segment: lab.py run campaign_segment --segment <lab run of a failed campaign>)` and the lab's line "the test printed
RESULT: FAIL without a FAIL line". The test only makes sense with `lab.py run campaign_segment --segment <run>` (A6,
BUG-1115): it plays a failed campaign segment again from the save it started from.

## Root cause
Observed: suite.py gave it the suites `()`, and `add()` turns any entry without a tag of the "only when asked for"
set (campaign, mixed, detector, refactor) into a `full` + `nightly` member, so the full suite (and the final check)
ran it without `CAMPAIGN_SEGMENT`. coop_campaign.py then printed only the RESULT line, no FAIL line naming the reason.
Its other `()` neighbours (campaign_tc2, campaign_tribe_share, ...) are meant for full; this one is not.

## Fix
suite.py: a `segment` tag in the "only when asked for" set and in SUITES; campaign_segment carries only it, so it runs
when named (`lab.py run campaign_segment --segment <run>`, A6 unchanged) and never in full / nightly. coop_campaign.py
without a segment prints `FAIL no segment given: ...` before its RESULT line. lab.py: an impact selection
(`lab.py run impact`, D5) without `--segment` leaves tests that need CAMPAIGN_SEGMENT out and says so (it picked
campaign_segment for any change of coop_campaign.py).

## Verification
* FAIL: lab 20261116-025154 (the full suite ran campaign_segment without a segment); labverdict_selftest's `suite:` check
  against rel/0.7.15's suite.py: FAIL.
* PASS: labverdict_selftest `suite: campaign_segment only in segment ...` (make test); `suite.select('full')` holds 832
  tests (834 before, without campaign_segment and chat_held_send_oldcheck).
* no-fail-run: a suite membership change (no game behaviour); proven by the unit check above.

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t1.
