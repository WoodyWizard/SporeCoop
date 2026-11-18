---
id: BUG-9085
title: Civilization economy and Tribal combat share a wire message ID
status: fixed
kind: desync
stage: Tribal
reported: 2026-11-07
source: Protocol review during BUG-9084
role: guest
versions: 0.7.3.6-duo23-dev
fixed_in: 0.7.3.6-lineages24-dev
branch: feat/lineages-civ-combat
commits: c2c6d0d5
tests: tools/tests/protocol_ids_selftest.py
signature: MSG_CIV_ECONOMY and MSG_TRIBE_COMBAT both equal 64
docs: docs/06-testing.md
related: BUG-9081
---

## Evidence

The economy addition reused wire ID 64, already assigned to Tribal combat.
coop.cpp dispatches Civilization messages before Tribal messages and returns,
so Tribal combat updates cannot reach their handler. Civilization tests alone
do not detect this regression. The registry test fails in bug9085-before.log.

## Fix

Assign economy ID 68 and religious influence ID 69, preserving Tribal ID 64.
Add a source-registry uniqueness check to the smoke suite. All players must use
the same Lineages build; the private Duo 23 economy payload is not compatible.

## Verification

FAIL: build/local/bug9085-before.log detects the two declarations using 64.
PASS: build/local/bug9085-pass.log and repeated registry runs verify unique
IDs. Source dispatch now selects Tribal combat separately; keep its world
gate explicit. Native economy regression lineages24-economy-n.log passes
route removal/recreation, changed meter and pricing, and stale-entry removal.
Full native network and Windows unit suites pass in lineages24-net-tests.log
and lineages24-win-tests.log.

PASS: build/local/lineages25-tribe-combat.log, actual packaged DLL on the
isolated DuoQALineagesCombatA pair. The historical four-seat save retains
25% HP / 10% damage bonuses: rival maximum HP 162 -> 283.5 reaches the
guest, and a native 5.2-point hit reaches the guest founder's current HP.
The new campaign defaults remain 15% / 5%. Both games exit with zero faults;
the newest crash report remains the preexisting 2026-11-06 22:34 report.

## History

- 2026-11-07: found the alias during religious-state review; assigned distinct
  IDs, added the registry test and reran economic and portable regressions.
