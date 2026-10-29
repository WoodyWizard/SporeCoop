---
id: BUG-0227
title: an armada wave resumed after the host's return can have normal-HP ships, be won without a fight, or ignore a raid lost meanwhile
status: open
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #161 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #161: https://github.com/WoodyWizard/SporeCoop/issues/161 (the full analysis, links and the scenario are there).

The BUG-0046 fix (9f9fd90, release 0.7.3.0) lets an armada contract's wave resume its own raid when the host comes back to the colony. The resume path has three gaps: the "HP raised once" set holds addresses of ships the game freed, a resumed wave keeps the "its ships appeared" flag from before, and a raid lost while the host was away is not counted (that last gap already existed before the fix). The test only counts raid missions, so it cannot see any of them. Severity LOW (contract balance, no crash).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
