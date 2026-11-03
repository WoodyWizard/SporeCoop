---
id: BUG-0514
title: Tribe: the guest's tribe members cannot pick up tools (weapons) from tool buildings the host built, so the guest's army stays weak
status: open
kind: bug
stage: Tribe
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: guest
versions: 0.7.3.6
fixed_in:
branch:
commits:
tests: 
signature:
docs:
related: BUG-0058, BUG-0483
---

## Report
Guest's mark (0.7.3.6, report 20261104-021224): 'cannot pick up the weapon'; a direct message: 'Tribe stage has the most bugs: the guest cannot really pick up tools from host-built buildings, so the guest's army is much weaker (main Tribe problem)'. Host screenshot at the mark: both tribes' members around the host's village with a tool building of the host's.
Target: 0.7.5 (Tribe).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not analysed. Inferred: a tool building is the host's object; on the guest it is a mirror the guest's members cannot take a tool from (the pick-up order goes to the guest's own game, which has no real tool rack there), and the host does not run the pick-up for the guest's members. Needs a guest -> host request (pick up tool X for member Y) as for the other tribe orders. Size: M.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage).
