---
id: BUG-9023
title: Newly built and removed Tribal tool racks do not reconcile on guests
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-04
source: Duo tool acceptance
role: guest
versions: unreleased after duo9
fixed_in: duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_tools.py
switch: tribe.tools
signature: building health snapshots cannot create a missing tool rack
docs: docs/re/tribe.md
related: BUG-9022
---

The building stream only finds an existing nearby building and updates its HP.
Complete per-village rack inventories are needed to create, move and remove
native tools. Skip a village while its planner contains an unfinished drag.
Parse/validate the full packet before native changes.

PASS `build/local/m2-tools-atomic.log`, disposable DuoQAM2ToolsAtomic4/5:
fix disabled reproduced the missing rack; enabled created the native guest
rack with correct owner/transform, moved it without replacing the object,
and removed it after host removal. Both games reported zero native faults.
This qualifies replication, not purchase cost, unlock rules or purchase UI.

The first fixture crashed the host by issuing CreateTool and SetTribe in
separate console frames. Native Update dereferenced the unset owner. The
fixture now performs creation/ownership/placement atomically, as the
production receiver already did. See docs/re/tribe.md for native evidence.
