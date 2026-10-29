---
id: BUG-0251
title: Tests: the cell and space suite tests compare the guest's received network position, not where the guest's game shows the host
status: open
kind: test
stage: All
reported: 2026-10-28
source: GitHub #185 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #185: https://github.com/WoodyWizard/SporeCoop/issues/185 (the full analysis, links and the scenario are there).

**Summary**
`coop_mode.py` runs the suite tests `cell`, `tribe`, `civ` and `space`. Its check "the guest shows the host at the new place" reads the position the guest last received over the network, not the position of the host's puppet in the guest's game. In Cell and Space the puppet is a real game object (a cell, a UFO). A guest that receives the host's states but whose game does not take them (puppet writes not applied, a puppet the game moved back, a wrong object) still passes. This is the bookkeeping check BUG-0042 was opened for; it was left in this script (commits 824813c, bb7154e and f7a4b75, release 0.7.3.0). In Tribe and Civ there is no game object to check: the shared "puppet" is a cursor marker inside the mod, so this issue covers Cell and Space only. Severity: low to medium (

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
