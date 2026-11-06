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
tests: tools/tests/duo_cell_reserved_food.py tools/tests/duo_creature_reserved_combat.py tools/tests/duo_tribal_reserved_combat.py
signature: Extra founders can earn the same DNA several times faster
docs: docs/DUO.md
related: BUG-9069
---

Two local games on four-seat rules qualified native Cell food, a Creature kill,
engaged creature health and damage, and rival Tribal warrior health and damage.
Still open are native Creature ally and extinction cards, editor refunds through
the UI, and four simultaneous founders. Live pirate-raid checks now cover Space
ship health and one scaled hit on each side. Cell whole-point bonuses use two
independent carries. Civ vehicle and turret combat is still open.
