---
id: BUG-9022
title: Tribal equipment changes are absent from guest snapshots
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Duo tool acceptance
role: guest
versions: unreleased after duo9
fixed_in: duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_equipment.py
switch: tribe.equipment
signature: host changes specialization and held item but guest remains unequipped
docs: docs/re/tribe.md
related: BUG-9021
---

Citizen snapshots contain neither specialization nor held item. Add a separate
world-gated, sequenced snapshot and use verified native setters to update
abilities and attach the model using the local creature's preferred grasper.
PASS build/local/m2-equipment-fixed.log, DuoQAM2EquipmentFixed4/5: disabled
fix reproduces stale specialization; enabled fix mirrors weapon, instrument,
utility and unequip. Equipped models exist through the native attachment
lookup on both peers. Citizen pointer/species remain unchanged; native faults
zero. This does not qualify tool purchases or equip orders.
