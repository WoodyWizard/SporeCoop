---
id: BUG-0229
title: a "ship disabled" state outlives a trip out of Space and destroys the ship on return, also the ship of a game loaded meanwhile
status: open
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #163 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #163: https://github.com/WoodyWizard/SporeCoop/issues/163 (the full analysis, links and the scenario are there).

The co-op "ship disabled" state (a lethal hit next to a partner only disables the ship for 20 s) keeps its timer running while the player is out of Space. On the first Space frame back, if the 20 s have passed, the mod kills the ship with the game's own death. After a load in between, that ship belongs to the newly loaded game. Severity LOW. The bug is older than release 0.7.3.0, which did not change its reach: `tick`, its early return and `end_down` are the same in 940fc74 and 5801eea. The release only added the BUG-0147 repair step and the BUG-0131 helper filter inside `down_tick`.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
