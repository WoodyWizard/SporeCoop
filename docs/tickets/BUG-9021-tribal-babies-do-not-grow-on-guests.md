---
id: BUG-9021
title: Existing Tribal baby mirrors do not become native adults
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Duo population acceptance
role: guest
versions: unreleased after duo9
fixed_in: duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_growth.py
switch: tribe.growth
signature: snapshot age 1 leaves the existing native citizen age at 0
docs: docs/re/tribe.md
related: BUG-9020
---

## Cause and candidate

guest_state stored the received age in its bookkeeping, then skipped an
already bound native citizen. Babies therefore remained babies locally.
Use native citizen GrowUp (modern vt+78 C248C0, plain ret) once for a 0-to-1
transition. It calls base growth, adds the citizen spatial to selectable
members, updates model/scale and adult AI flags, then clears the baby hit
sphere. It preserves the citizen pointer and founding species. Unbound
local candidates must have the same native age as the arriving snapshot.

PASS build/local/m2-growth-fixed.log on DuoQAM2GrowthFixed4/5: the test
reproduces the stale native age with tribe.growth off, then enables the fix
and verifies the same citizen is an adult and appears once in the selectable
collection on both peers, with its original species and zero native faults.
The separate unpatched control is build/local/m2-growth-control.log.

The test creates a baby with SpawnMember(0), grows
the host through its verified native method, then checks guest native age,
species, pointer stability and exactly one selectable entry.
