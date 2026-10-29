---
id: BUG-0222
title: Tests: civ_reload and civ_sw_vote have checks that pass without their fix or only read the mod's own variables
status: open
kind: test
stage: All
reported: 2026-10-28
source: GitHub #156 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #156: https://github.com/WoodyWizard/SporeCoop/issues/156 (the full analysis, links and the scenario are there).

**Summary**
Two lab tests added for the Civ fixes of release 0.7.3.0 do not prove what their docstrings say.
- In `coop_civ_reload.py` (BUG-0040, BUG-0059), two of the checks cannot fail on the code without the fix, and the speed the boost writes into the game is never read.
- `coop_civ_sw_vote.py` (BUG-0117) checks a mod variable that is set just before the game call, not what the game fired. The host's own superweapon path, which the fix also changed, has no test.

Severity: low (tests only). A later regression in these paths would pass the lab (CLAUDE.md 3: "a test you never saw fail proves nothing", "check what the other player sees").

**Where**
- `coop_civ_reload.py`, the checks after the load and after the leave: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260fa

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
