---
id: BUG-0226
title: spice sent with the co-op spice gift is lost when it comes back while the sender is not in Space
status: open
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #160 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #160: https://github.com/WoodyWizard/SporeCoop/issues/160 (the full analysis, links and the scenario are there).

The co-op spice gift (the co-op window's Give button in Space gives 10 spice; `coop.space give <n>` in tests; spacecoop.cpp K_GIFT / K_GIFT_BACK) takes the spice from the sender's cargo first. If the receiver has no room, the spice comes back, but when the sender is outside Space at that moment (galaxy menu, an editor, a load) `add_spice` fails and the spice is dropped without a notice. BUG-0058 (fd814d3, release 0.7.3.0) fixed this pattern for the trade feature (space_trade.cpp) only. Severity LOW, data loss of a few spice units; not a regression (the code is the same as in 940fc74).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
