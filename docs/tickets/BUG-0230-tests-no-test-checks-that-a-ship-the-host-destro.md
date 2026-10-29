---
id: BUG-0230
title: Tests: no test checks that a ship the host destroys disappears on the guest under chunked ship state, and the defenders test asserts the leftover ship
status: fixed
kind: test
stage: All
reported: 2026-10-28
source: GitHub #164 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-space
commits: c5d89f1, 730acef
tests: space_fleet (coop_space_fleet.py part 3), space_defenders (part 2 a, b, c)
signature:
docs:
related: BUG-0037, BUG-0111, BUG-0218
---

## Report
From GitHub issue #164: https://github.com/WoodyWizard/SporeCoop/issues/164 (the full analysis, links and the scenario are there).

The BUG-0037 fix (c5fe0b8, release 0.7.3.0) made the guest wait for every chunk of a ship-state cycle before it removes a mirror. Its test only checks the half "nothing is removed after a lost chunk". No test checks the other half, "a ship the host really destroyed is removed", when the state comes in 2 or more chunks, so a fix that never removes anything would pass. In the same release, part 2 of `coop_space_defenders.py` (BUG-0111) asserts that the old ship is left in the game, which is the behaviour #152 reports. Severity LOW (test gaps).

## Root cause
Test gaps (confirmed): `coop_space_fleet.py` never destroyed a host ship under 2 chunks, so a removal pass that never runs passed; the defenders test asserted the leftover ship of BUG-0218.

## Fix
Part 3 of `coop_space_fleet.py`: 27+ host ships (2 chunks), two of them (first and last listed) destroyed with `coop.fleet shoot <id> 1000000` while chunk 0 is lost for 3 s: both mirrors gone within 2 s after the drops, every other mirror kept its object, mirror count equals the host's; with chunk 1 lost for good a destroyed ship's mirror stays (by design) and goes within 2 s when the chunk comes again. The defenders test was reworked with BUG-0218 (cases a, b, c).

## Verification
PASS out/lab/20261028-225821 (space_fleet, space_defenders). FAIL with the mutation of the issue (`if (chunks > 1) return;` before the removal pass, not committed): out/lab/20261028-232213 (both mirrors stayed, 29 / 27, the steady-loss mirror stayed).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-space: fleet test part 3 (seen failing with the removal mutation), defenders test reworked with BUG-0218.
