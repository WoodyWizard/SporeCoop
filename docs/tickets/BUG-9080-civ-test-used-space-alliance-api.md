---
id: BUG-9080
title: Civilization test setup passes political IDs to Space empire API
status: wontfix
kind: test
stage: Civilization
reported: 2026-11-06
source: TradeControlsC diagnostic setup
role: host
versions: duo21-dev
fixed_in:
branch: feat/lineages-civ-combat
commits:
tests: tools/crash_triage.py
signature: 3010a413:SporeApp.exe Simulator::cRelationshipManager::IsAtWar+0x22
docs: docs/re/space.md
related: BUG-9079
---

## Report and cause

A diagnostic console call attempted DeclareAlliance(manager, 0x2231, 0x2232)
to make the trade fixture friendly. That API takes two cEmpire pointers,
not political IDs, and belongs to Space diplomacy. Its DeclarePeace call
reached IsAtWar and dereferenced 0x2232 + 0x84, causing the test host fault.
The existing docs/re/space.md signature already states the pointer types.
The diagnostic caller failed to consult that full signature.

## Verification

crash-20261106-223422-i4.txt, Steam executable 3010a413, Duo 21.
tools/crash_triage.py identifies IsAtWar+0x22 and DeclareAlliance+0x28;
the fault data address is 0x22B6. Native disassembly confirms argument
dereferences at +0x84. The guest had zero faults and was closed normally.
No save or production code was changed by this failed setup attempt.

No gameplay patch is appropriate. Do not call Space empire state APIs
with Civilization political IDs. The call is not part of the regression
test; future fixtures must use verified Civilization relationship paths.

