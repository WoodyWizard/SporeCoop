---
id: BUG-9072
title: Qualify hostile combat and shared rewards at reserved campaign capacity
status: open
kind: bug
stage: all
reported: 2026-11-06
source: Four-founder difficulty scaling
role: host guest
versions: duo10-dev
fixed_in:
branch: duo-species
commits:
tests: tools/tests/duo_cell_reserved_food.py tools/tests/duo_creature_reserved_combat.py tools/tests/duo_tribal_reserved_combat.py tools/tests/duo_civ_vehicle_combat.py tools/tests/duo_civ_turret_combat.py tools/tests/duo_civ_military_production.py
signature: Extra founders can earn the same DNA several times faster
docs: docs/DUO.md
related: BUG-9069
---

Two local games on four-seat rules qualified native Cell food, a Creature kill,
engaged creature health and damage, and rival Tribal warrior health and damage.
Still open are native Creature ally and extinction cards, editor refunds through
the UI, and four simultaneous founders. Live pirate-raid checks now cover Space
ship health and one scaled hit on each side. Cell whole-point bonuses use two
independent carries. A two-peer Civilization fixture measured a four-seat rival
military hit at 11.5 instead of 10, mirrored its boosted maximum HP, and
restored the base maximum on disconnect. A native rival city turret dealt 11.5
instead of ten to a founder vehicle. Local peers agreed on wounded turret health,
its 870 maximum (600 offline), and new and removed rival wall emplacements.
The guest drag-selection and right-click attack passed on a separate local
pair. Both peers showed the same wound and the host rejected an attack whose
target died during 1.5 seconds of simulated network delay. Four simultaneous
founders still need a separate run.

In the four-seat military production test, the rival's purpose allowance rose
from seven to ten. With the simulation paused and a fixed target and budget,
the native AI held at 28,000 ms and purchased eight vehicles at 28,001 ms.
After a founder dealt lethal damage to one, the AI spent another 3,000 to
replace it. The host and guest agreed on the ten network IDs after replacement.
An installed two-seat ZIP gave the same nation eight military slots, with its
ordinary purchase held at 36,000 ms and admitted at 36,001. After a second
combat loss the unpaused AI refilled the slot on its own in approximately 37
real seconds. A mixed-fleet fixture then filled the nation's 36th position
with a nonmilitary vehicle. The funded military purchase was refused and the
native budget retained its full balance.

Duo 16 adds military overflow. At four seats and 100% scaling, the same
funded AI bought three additional military vehicles in a 36-vehicle mixed
fleet. It paid for a replacement after a loss. Both peers agreed on the
39 live IDs, and the ZIP-installed DLL cold-loaded all 39 native serials
and purposes. At 600% custom scaling the host filled 47 slots, refused a
48th purchase without a charge, and matched the guest's complete fleet
after replacement. Economic and religious slot checks returned zero above
the native total.

The installed DLL cold-loaded all 47 native identities in the stress save.
In a two-seat campaign the unpaused AI replaced a lost 37th vehicle on its
own. This last fixture started the clock one second before the 36-second
threshold to separate production from a rival capturing its target city.
