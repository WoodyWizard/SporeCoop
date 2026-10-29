---
id: BUG-0221
title: the game's own death now runs inside a session when the only partner is knocked out, and no test watches the respawn that follows
status: open
kind: bug
stage: Cell
reported: 2026-10-28
source: GitHub #155 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #155: https://github.com/WoodyWizard/SporeCoop/issues/155 (the full analysis, links and the scenario are there).

Since BUG-0131 (#110, commits 8f749d0, 153ec67, release 0.7.3.0), a knocked-out partner no longer counts as a helper. With two players, a lethal hit on the second one therefore runs the game's own death and respawn whenever the first is still knocked out. This is the intended behaviour of the ticket ("if nobody can help, the normal death happens"). The level rebuild that follows a death is documented, and an in-session rebuild with partners in the pool already works and is tested: the mod triggers one itself when it applies a player's own cell. What is new and unverified is the death-specific part (the death UI, the death count, the hatch intro) with a knocked-out partner in the pool, and the BUG-0131 test stops 1 s after the death. Severity LOW: a test gap plus a short runtime check.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
