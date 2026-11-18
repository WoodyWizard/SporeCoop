---
id: BUG-9007
title: The host rewinds a two-founder Tribal world while the guest village survives
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-03
source: Duo fork Windows lab
role: host
versions: 0.7.3.6-duo7
fixed_in: 0.7.3.6-duo8
branch: duo-species
commits:
tests: tools/tests/duo_tribe_survival.py
signature: Native message 05C80207 plays TRG_PRE_PopDestroyed despite a healthy second founding village
docs: docs/re/tribe.md#tribe-founder-recovery
related: BUG-0172
---

The disposable Clyth copy DuoQATerritory4 has no healthy host citizens and three
healthy guest citizens. Both huts have 400 health and both tribes have
mbDefeated false. The host still enters FBCD6489 and offers the population-loss
rewind. Suppressing the caption alone would leave the preceding camera and
loss-state changes in place. Intercept the narrowly identified native event.

The user chose to leave a conquered village with its conqueror and refound the
defeated species elsewhere with help from its partner. Population loss with an
intact hut and conquest must remain separate cases.

The native population message is intercepted before its loss-state changes.
The clean sequential test preserved guest movement in both games. Disabling
tribe.founder_survival reproduced the native rewind on DuoQAPopulationNegative4/5.
With the fix enabled, killing both founding populations still produced the
ordinary rewind on DuoQAPopulationBoth4/5. Evidence is under out/duo8.
