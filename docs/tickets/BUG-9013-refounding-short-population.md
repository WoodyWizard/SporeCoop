---
id: BUG-9013
title: The refounding prototype requests three villagers but creates two
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-03
source: Duo native refounding prototype
role: host
versions: 0.7.3.6-duo9 development
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: build/local/refound-native.py
signature: SpawnTribe count 3 produces two native citizens after ownership transfer
docs: docs/re/tribe.md#tribe-refounding-prototype
related: BUG-9009
---

## Report

The manual native prototype assigns the defeated guest village's hut and food
mat to another tribe, then rebuilds the original guest cTribe at another site.
SpawnTribe(..., count=3, food=15, false) produces two citizens of the correct
species. It is not integrated into player-facing gameplay.

## Evidence

build/local/refound-native.log reports members 2. Native SpawnTribe subtracts
the length of the tribe's +354 selectable vector from the requested count.
The precise remaining object or interface has not yet been identified.

## Next check

Inspect both native membership vectors before and after defeat, ownership
transfer and tool cleanup on a fresh copied profile. Verify the native removal
path before integrating rebuilding. Do not clear vectors or change counts to
hide an unresolved ownership reference.

## Follow-up (2026-11-04)

The stale entry is the removed citizen's +C0 spatial interface. Retaining the
citizen across RemoveMember, then native C95280 selective removal, releases it
and permits three actual citizens. Evidence: m2-refound-inspect.log and
m2-refound-clean-control.log. Reusing that tribe still fails later population
stability (BUG-9014). The successful fresh-tribe/retirement prototype is
documented in docs/re/tribe.md#tribe-refounding-retirement. Integration remains
open; increasing the spawn request is not the solution.

Integrated resolution: BUG-9017/9018 use the full native factory and retire
the old tribe instead of recycling its stale membership state. Native
ownership, three-member population, walking and cold-load regressions pass.
The fresh Vantiegan campaign also rebuilds its captured host through the
partner action in tribal-final-rebuild-c.log, then completes all five
objectives with both founder populations present. The failed reuse prototype
is not the player-facing rebuild implementation.
