---
id: BUG-0253
title: Tools: lab.py kill --lanes N now ends only lane N, and --lanes 1 silently does nothing
status: fixed
kind: tooling
stage: All
reported: 2026-10-28
source: GitHub #187 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-tools
commits: f180926
tests: make test (lab_selftest.py kill_args)
signature:
docs: docs/10-dev-pipeline.md, docs/06-testing.md#testing-lab-free-lane
related:
---

## Report
From GitHub issue #187: https://github.com/WoodyWizard/SporeCoop/issues/187 (the full analysis, links and the scenario are there).

**Summary**
BUG-0103 changed `lab.py kill` so that it no longer touches lane 1 (the owner's own install) by default. That part is right. But the same change also changed what `--lanes` means:
- Up to 0.7.2.4, `--lanes N` meant "lanes 1..N", like `lab.py run --lanes N`. Now it is a comma list, so `lab.py kill --lanes 3` ends lane 3 only and leaves lane 2's games running.
- `lab.py kill --lanes 1` ends nothing and prints nothing, unless `--include-lane-1` is given.

Severity: minor (developer tooling). Leftover games keep their ports and their bottle, and the next test on that lane can fail at start for a reason nobody sees.

**Where**
- The new options: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/tools/lab.py#L397-L400
- The kill loop: https://gith

## Root cause
Confirmed on today's code: BUG-0103 turned `--lanes` into a comma list, so `--lanes 3` meant lane 3 only (0.7.2: lanes 1..3) and `--lanes 1` an empty list after lane 1 was dropped, silently (exit 0).

## Fix
`lab.py kill`: a bare number in `--lanes` is refused (exit 2) with the unambiguous forms; `--lane N` (repeatable), `--upto N` (lanes 2..N), `--lanes 2,3`; lane 1 only with `--include-lane-1` ("lane 1 left out" otherwise); refuses lanes outside 1..10; prints the lanes it ends; exit 1 when none is left. No switch (tooling).

## Verification
Unit test in make test: `lab_selftest.py` case kill_args. FAIL: the same self-test against the lab of b423f56 (`lab_selftest.py --tree <b423f56 copy>`): `--lanes 3` killed lane 3, `--upto` killed nothing, `--lanes 1,` exit 0 with nothing printed, `--lane 11` killed lane 11 (6 FAIL lines). PASS: on fix/w5-tools, all 8 cases ok.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-tools (f180926): kill_args FAIL on b423f56's lab, PASS with the fix.
