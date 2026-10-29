---
id: BUG-0217
title: after the host visits an editor, the co-op epic is no longer tracked and goes into the shared save
status: open
kind: bug
stage: Tribe
reported: 2026-10-28
source: GitHub #151 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #151: https://github.com/WoodyWizard/SporeCoop/issues/151 (the full analysis, links and the scenario are there).

**Summary**
The host opens the editor in the Tribe stage (an evolve vote, or the game's own editor entry). From then on the mod forgets the co-op epic creature and its herd, but the creature stays alive in the world. The world share that follows the editor saves that live epic into the host's save and every guest's copy. That is exactly what BUG-0121 says no longer happens. Eight minutes later a second co-op epic is spawned next to the untracked one. Severity: medium (wrong behaviour, mod objects in saves).

This is a regression in release 0.7.3.0. ba9dc46 (BUG-0081) and 421bfe0 (BUG-0121, BUG-0067) added `clear_coop_tables(false)` to `tribe::on_world_change()`. That function also runs when an editor opens, because coop.cpp treats "no local entity" as a world change. Before these commits (

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
