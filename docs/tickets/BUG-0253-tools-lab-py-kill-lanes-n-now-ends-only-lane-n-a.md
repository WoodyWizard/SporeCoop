---
id: BUG-0253
title: Tools: lab.py kill --lanes N now ends only lane N, and --lanes 1 silently does nothing
status: open
kind: tooling
stage: All
reported: 2026-10-28
source: GitHub #187 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
