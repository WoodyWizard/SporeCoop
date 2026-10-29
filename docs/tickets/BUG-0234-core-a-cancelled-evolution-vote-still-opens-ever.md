---
id: BUG-0234
title: Core: a cancelled evolution vote still opens every guest's own editor, and a pending own editor survives the session end
status: open
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #168 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #168: https://github.com/WoodyWizard/SporeCoop/issues/168 (the full analysis, links and the scenario are there).

**Summary**
When the host leaves the Creature stage during an evolution vote, the host cancels the vote (BUG-0043), but every guest still opens its own evolution editor, usually on the next frame. The guest cannot tell a cancelled vote from a passed one. Two more states are not reset when they should be:
- The guest's pending own editor (`g_own_editor_due`) outlives the session. A guest that leaves, or loses the host, just after a vote still gets its editor opened with no session.
- The Give recipient pick (`g_give_pick`) outlives its player and the session. It then points at whoever gets that id next.

Severity LOW-MED: an editor nobody asked for, while the host is in the galaxy menu. The BUG-0043 fix (fix/w1-core, 066df15) left this incomplete, and BUG-0033 (a387323) did not add these st

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
