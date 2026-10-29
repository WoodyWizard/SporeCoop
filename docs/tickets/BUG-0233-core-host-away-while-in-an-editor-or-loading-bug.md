---
id: BUG-0233
title: Core: "host away while in an editor or loading" (BUG-0205) fires in every stage and during the host's stage change, and guests' DNA kept meanwhile is lost or paid into another world
status: open
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #167 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #167: https://github.com/WoodyWizard/SporeCoop/issues/167 (the full analysis, links and the scenario are there).

**Summary**
Since 0.7.3.0, a guest counts the host as away once the host has been in an editor or on a loading screen for 4 s (BUG-0205, fix/w4-census, 2b90664). This goes wrong in three ways:
- The rule applies in every stage, also in Cell and Space, where no module acts on it. Those guests get "you play on alone" / "is back" notices for nothing.
- It also fires while the host loads the next stage.
- The DNA a guest earns meanwhile is sent to a host that is already in Tribe. There it waits in `g_dna_owed`, which has no world or stage tag. It is never paid there, but it is paid into a later Creature world if the host loads one in the same session.

Severity LOW: wrong notices, and a little DNA lost or put into the wrong world. The documentation side (CHANGELOG 0.7.3.0 lists BUG-0205 as fix

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
