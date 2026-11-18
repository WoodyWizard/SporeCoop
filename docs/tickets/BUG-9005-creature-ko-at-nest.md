---
id: BUG-9005
title: A knocked-out guest can heal at the nest while waiting for a rescue
status: fixed
kind: bug
stage: Creature
reported: 2026-11-03
source: Duo fork Windows lab
role: guest
versions: 0.7.3.6-duo6 development
fixed_in: 0.7.3.6-duo6
branch: duo-species
commits:
tests: tools/tests/duo_creature_rescue.py
signature:
docs: docs/re/creature.md#creature-duo-respawn
related:
---

On a cold load of Brois, a lethal hit at the nest set the guest's downed flag.
The game subsequently reported 10/10 HP with that flag still set. A development
build required remote HP <= 1 in addition to the knockout flag and rejected the
begin request. The guest waited at zero seconds while both games retained 92 DNA.

Duo 6 reapplies 1 HP while down and uses the matching planet, seat, knockout
number and downed flag to accept the guest's request. The cold-load repeat on
Brois held 1 HP midway through the window. A revive kept 92 DNA. The timeout
then left both games at 82. Reversing roles left both games at 73 after one debit.
