---
id: BUG-0237
title: entering the cell editor within 2-3 s of a revive or respawn can leave the cell invulnerable afterwards
status: open
kind: bug
stage: Cell
reported: 2026-10-28
source: GitHub #171 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #171: https://github.com/WoodyWizard/SporeCoop/issues/171 (the full analysis, links and the scenario are there).

BUG-0070 (#48, commit e7ab004, release 0.7.3.0) resets our knockout state when the Cell stage is left. When the stage is left through the cell editor, there is no avatar at that moment. The reset clears the timer that would end the 2 s (revive) or 3 s (respawn) invulnerability, but it cannot clear the avatar's `+0x111`. If the same avatar object is back after the editor, nothing ever clears the flag, and the player's cell takes no damage for the rest of the stage. Before 0.7.3.0 the timer survived the editor and cleared the flag on return. Severity LOW, PLAUSIBLE: it depends on the avatar surviving the editor, and the docs suggest the level is rebuilt after it.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
