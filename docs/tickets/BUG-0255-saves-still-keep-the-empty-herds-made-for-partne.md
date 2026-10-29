---
id: BUG-0255
title: saves still keep the empty herds made for partners' puppets, and the save filter can reuse an old object list for a second save
status: open
kind: bug
stage: Creature
reported: 2026-10-28
source: GitHub #189 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #189: https://github.com/WoodyWizard/SporeCoop/issues/189 (the full analysis, links and the scenario are there).

**Summary**
BUG-0020 (commits 45acba3, 0d79bae, 2be1123, 3699255, 0a6d447, release 0.7.3.0) keeps our puppets, proxies and their herds out of saves. Two gaps remain in `mods/coop/savestrip.cpp`:
- A herd made for a puppet is stripped only while a puppet is in it. Once the partner leaves or the puppet is rebuilt, the empty herd stays in the world and goes into every save.
- The list of our objects is built again only when the serializer address changes or 0.5 s pass between writes. A second save soon after, through a serializer at the same address, uses the old list.
Severity: minor (no crash known; a herd without an archetype in the player's own save, and a narrow risk of writing a game object as null). Both are incomplete parts of the fix, not regressions against 940fc74. Separately, the 

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
