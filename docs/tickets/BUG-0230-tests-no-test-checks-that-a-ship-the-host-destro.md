---
id: BUG-0230
title: Tests: no test checks that a ship the host destroys disappears on the guest under chunked ship state, and the defenders test asserts the leftover ship
status: open
kind: test
stage: All
reported: 2026-10-28
source: GitHub #164 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #164: https://github.com/WoodyWizard/SporeCoop/issues/164 (the full analysis, links and the scenario are there).

The BUG-0037 fix (c5fe0b8, release 0.7.3.0) made the guest wait for every chunk of a ship-state cycle before it removes a mirror. Its test only checks the half "nothing is removed after a lost chunk". No test checks the other half, "a ship the host really destroyed is removed", when the state comes in 2 or more chunks, so a fix that never removes anything would pass. In the same release, part 2 of `coop_space_defenders.py` (BUG-0111) asserts that the old ship is left in the game, which is the behaviour #152 reports. Severity LOW (test gaps).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
