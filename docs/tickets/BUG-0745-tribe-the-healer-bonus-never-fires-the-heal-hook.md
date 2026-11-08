---
id: BUG-0745
title: Tribe: the healer bonus never fires, the heal hook reads the maximum health at +0x5D4 (1.0)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-08
source: fix/w46-g4b-coverage, coop_tribe_healer.py lane 3 2026-11-08
role: host
versions: 0.7.10 and earlier
fixed_in: 0.7.11
branch: fix/w46-g4b-coverage
commits: 3f55f0d2
tests: tribe_healer
signature: tribe co-op heals=0 after a full heal of a partner member
docs: docs/re/tribe.md#tribe-healer-runtime
related: BUG-0199
---

## Report
Found by the new coverage test `coop_tribe_healer.py` (BUG-0199): the host's healer brought a guest's member from 56 to
112 health (the guest's game showed it too), yet the shared tribe food did not change and `coop.status` said
`heals=0`. The co-op healer bonus (+5 food for a full heal of a partner's member, tribe.cpp co-op mechanics) has never worked
in the GOG 2024 build (and was never tested before).

## Root cause
✔ The heal hook (tribe.cpp h_HealTick on the heal tick 0xDB9AC0) took the patient's maximum from patient+0x5D4
(combatant +0x2C, from the static recipe in docs/re/tribe.md C). At runtime that field is 1.0 (mem.read of the patient,
lane 3), so `hp0 < max` was never true and the bonus never fired. The maximum is cCombatant::GetMaxHitPoints (vtable
0x58), which game::max_health already reads (game.cpp notes the same +0x2C = 1.0 for avatars).

## Fix
h_HealTick reads the maximum with game::max_health. Switch `tribe.heal_max` (off: the old +0x5D4 read).

## Verification
* `SPORE_FIX_OFF=tribe.heal_max`: FAIL, lab 20261108-063242 lane 3: the patient healed to full on both games, food
  15 -> 15 on both.
* PASS 3 of 3, lab 20261108-063444 lane 3 (~2 min each): food 15 -> 20 on both games within a second of the full heal.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-g4b-coverage): found by tribe_healer, fixed (game::max_health), switch tribe.heal_max; off FAILS (20261108-063242), 3 of 3 PASS (20261108-063444).
- 2026-11-08: released in 0.7.11.
