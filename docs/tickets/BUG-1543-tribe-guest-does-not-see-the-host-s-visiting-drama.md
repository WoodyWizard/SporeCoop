---
id: BUG-1543
title: Tribe: guest does not see the host's visiting drama UFO spaceship
status: open
kind: desync
stage: Tribe
reported: 2026-11-24
source: Discord bug report 2026-11-24 (SporeCoop-report-20261124-215842)
role: guest
versions: 0.7.16, 0.7.16.1, 0.7.99
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-ufo-mirror
related: BUG-0486, BUG-0558
---

## Report
In Tribe stage, when the host gets a visiting UFO spaceship drama event, the guest does not see the ship.

## Root cause
`drama.cpp` blocks the guest's local UFO drama event (`g_mask = 1u << 2`) in both Creature and Tribe stages (BUG-0486),
but the host-to-guest UFO mirror (`BUG-0558`, `player_rules.cpp`) gates both sending and receiving on
`game::in_creature_stage()` only, leaving Tribe guests without the host's UFO mirror.

## Plan (0.8)
Extend `player_rules.cpp` `K_UFO` host send and guest mirror to cover Tribe stage (`modes::kTribe`) as well as Creature
stage, with a Wine lab test and OFF-twin.
