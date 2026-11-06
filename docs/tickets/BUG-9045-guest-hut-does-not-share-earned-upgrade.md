---
id: BUG-9045
title: Guest village retains its old native hut level after team progression
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-05
source: Shared progression review following native population capacity qualification
role: guest
versions: duo10-dev
fixed_in: unreleased
branch: duo-species
commits: 52061cc
tests: tools/tests/duo_tribe_hut_upgrade.py
switch: tribe.shared_hut_upgrade
signature: earned home community level1, guest community level0
docs: docs/re/tribe.md
related: BUG-9039 BUG-9030
---

BUG-9039's native capacity control observed home level1 and guest level0.
Routing the birth-cap query through the campaign tribe fixed population, but
does not update the guest's native hut model or maximum health. Verify the
live hut state before adding reconciliation. Both villages must share earned
upgrades, while keeping their species, ownership and purchases.

Existing verified methods: Community_SetLevel FF0260 thiscall(community,int),
Tribe_UpdateHutModel C99DC0 thiscall(tribe), primary hut tribe+368. The refresh
uses native level getter A1AC90 at +EF, selects its native hut model, updates
maximum health+14C and layout12. It requires a live primary hut and runs
outside the frame. Do not call it during loss/rebuild, a cinematic or loading.

FAIL m2-hut-upgrade-control-a.log: on both games home level1/max750/current750,
guest level0/max400/current400. PASS m2-hut-upgrade-fixed-a.log: both founders
reach level1/max750 with unchanged faction IDs. Zero native faults on both.
The host calls the existing verified native setter/refresh outside-frame,
then the existing confirmed-save transfer preserves the new level for the guest.

CORRECTION: the initial implementation comment assumed the native refresh
preserves current health. Runtime evidence shows it refills to the new maximum,
400 to750 here. Preserve Spore's native upgrade behavior. The level mismatch
guard applies this once per earned change, not every frame or on every join.

## Verification

FAIL build/local/m2-hut-upgrade-control-a.log (guest level0/max400).
PASS build/local/m2-hut-upgrade-fixed-a.log (both level1/max750).
PASS build/local/m2-hut-upgrade-cold-a.log (same saved values after process
restart, repair disabled on both games before loading). Both passes exit with
zero native faults. The control switch must be off before loading the old
checkpoint; turning it off after repair does not undo the native saved upgrade.
