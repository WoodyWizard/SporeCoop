---
id: BUG-0216
title: Docs: release notes 0.7.3.0 and README claim more than the code does (an open ticket as fixed, the protocol, GOG 2016 fixes that are off)
status: open
kind: tooling
stage: All
reported: 2026-10-28
source: GitHub #150 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #150: https://github.com/WoodyWizard/SporeCoop/issues/150 (the full analysis, links and the scenario are there).

**Summary**

CHANGELOG 0.7.3.0 and the README make claims that do not match the code:
- an open ticket (BUG-0205) is listed as fixed;
- a behaviour change for Tribe and Civ guests is not mentioned;
- the notes say "the network protocol did not change", although 0.7.3.0 added a wire flag that 0.7.2.x ignores;
- the README's compatibility line still stops at 0.7.2;
- the list of fixes that switch themselves off on GOG 2016 misses two fixes of this release (BUG-0188 and BUG-0020).

The last one cannot be caught today: `tools/re_lint.py` only sees symbol names written literally inside `sym("...")`, so a name looked up through a table is invisible to the frozen-build report that BUG-0108 relies on. Severity: low (docs and tooling). This issue proposes a re_lint rule and a release-check step, so

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
