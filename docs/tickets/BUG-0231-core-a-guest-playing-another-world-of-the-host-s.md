---
id: BUG-0231
title: Core: a guest playing another world of the host's stage is handled differently by each module (Civ still applies the host's state and founds cities, Tribe freezes, DNA goes to the host)
status: open
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #165 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #165: https://github.com/WoodyWizard/SporeCoop/issues/165 (the full analysis, links and the scenario are there).

**Summary**
BUG-0130 (fix/w2-world, 7e2173d) was meant to stop the host's world being applied to a guest that plays another world of the host's stage. Each module decides "the guest is elsewhere" differently, or not at all:
- Civ still applies the host's vehicle and city state. It kills the guest's own vehicles and, since BUG-0120, founds cities in the guest's own world.
- Tribe treats the guest as a mirror guest, so its own tribe stands frozen and ignores orders.
- Creature DNA the guest earns in its own world goes to the host's world.
- A failed share is never retried in that phase, so the state can last.

Each part is LOW. Together they are a MED design problem: the fix needs one rule that every module follows. With default settings (`share_world=1`, `auto_load_world=1`) the window is s

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
