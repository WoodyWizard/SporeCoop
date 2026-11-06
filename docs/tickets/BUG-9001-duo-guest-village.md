---
id: BUG-9001
title: Preserve each founder's species and give the guest independent tribal control
status: fixed
kind: feature
stage: Tribe
reported: 2026-11-03
source: Duo fork Windows lab
role: guest
versions: 0.7.3.5-duo1
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_villages.py
signature:
docs: docs/re/tribe.md#tribe-duo-guest-village
related: BUG-0381
---

## Report
The guest's species can found a distinct NPC village. The native player tribe, selection,
food, and command authority must map to that village before normal play can use it.

## Verification
The two-instance Windows test checks actual citizen species, village size, duplicate
founding and a temporary switch of the native player tribe. Both villages survived a
native save, process restarts and a guest rejoin. The guest controls and food still
belong to the original tribe during normal play. The Duo 2 opt-in walking mode recognizes
both factions, and `tools/tests/duo_orders.py` verifies native movement on both games,
selection isolation, the in-game march and revocation after the species changes.
The Duo 3 cold Windows run also passed `tools/tests/duo_food.py`, including an actual
fruit deposit into the guest village and correction of a local-only food change when
the home village started empty. Tool purchases, combat and full campaign progression
remain open.

Final duo qualification (2026-11-05): the integrated campaign now has
automatic founding, independent native village ownership and orders, combat,
food/tools, native outfitters and recovery. Either founder earns the shared
campaign victories, unlocks and native history. Fresh five-objective coverage
and native host input pass (tribal-final-campaign-h.log,
tribal-final-five-readback.log, tribal-host-final-b.log). Native graduation
and reconnect retain both original species and current outfits; the actual
packaged DLL also cold-loads the two capitals and matching custom designs
(tribal-package-cold-final.log). This closes the two-species Tribal feature;
internet compatibility and the inherited upstream movement issues are not
claimed resolved by these component tests.
