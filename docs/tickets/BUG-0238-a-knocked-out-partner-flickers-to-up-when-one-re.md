---
id: BUG-0238
title: a knocked-out partner flickers to "up" when one refresh is late, counts as a helper meanwhile, and replays its death effect
status: open
kind: bug
stage: Cell
reported: 2026-10-28
source: GitHub #172 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #172: https://github.com/WoodyWizard/SporeCoop/issues/172 (the full analysis, links and the scenario are there).

BUG-0061 (#39, commit e7ab004, release 0.7.3.0) ends a remote knockout that its owner has not refreshed for 2 s (`kKoStale`). The owner refreshes every 0.25 s over the reliable channel, so a single late refresh (a retransmit under loss, a hitch on either machine) is enough. The partner is then drawn opaque and counts as "up". That includes the BUG-0131 helper question, so for that moment a knocked-out partner can rescue or revive again. The next refresh is seen as a new knockout and replays `cell_death_oneshot`. Severity: wrong behaviour, minor.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
