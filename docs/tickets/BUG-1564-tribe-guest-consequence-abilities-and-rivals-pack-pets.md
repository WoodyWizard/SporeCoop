---
id: BUG-1564
title: Tribe: guest consequence abilities and Creature-to-Tribe pack pet transfer in Tribe Rivals
status: fixed
kind: feature
stage: Tribe
reported: 2026-11-25
source: 0.8.1 Rivals isolation audit
role: both
versions: 0.8.0.2
fixed_in: 0.8.1
branch: w081/tribe-consequence-pets
commits: e27830c6
tests: rivals_tribe_abilities_pets, rivals_tribe_abilities_pets_off
signature:
docs: docs/11-rivals.md, docs/re/tribe.md
related: BUG-1548, BUG-1554, BUG-1557, BUG-1560
---

## Report
1. Tribe stage consequence abilities (earned from Cell and Creature consequence cards: Traps, Refreshing Storm, Flying Fish, Fire Bombs, Fireworks, Beastmaster) were not routed from guests to the host for the guest's own tribe and own consequence cards.
2. On `Creature -> Tribe` transition in Tribe Rivals, each player's recruited Creature pack species were not carried over as domesticated pets at their own village (whereas solo Spore transfers pack creatures into village pets).

## Root cause
- `tribe.cpp` / `tribe_host.cpp` did not intercept Tribe consequence ability slots (`ability+0x0C == 4`, `ability+0x08` in `52..57` from table `0x0169DE4C`) on guests or forward them to the host to run `SporeCoop::Ability_Use` (`0x00D1E930`) against the sender's `cTribe` chieftain.
- `tribe_rivals.cpp` took over an NPC tribe for each guest without snapshotting each player's Creature-stage pack species (`npc::pack_species_of`) or spawning matching domesticated pets (`cCreatureAnimal::Create` + `SporeCoop::DomesticateAnimal` `0x00DB5E10`) into the player's village domesticated herd (`tribe+0x19CC`).

## Fix
- Reverse-engineered Tribe consequence ability table `0x0169DE4C` (`52..57`: Traps, Refreshing Storm, Flying Fish, Fire Bombs, Fireworks, Beastmaster), `0x00D1F400` card unlock checks, and `0x00D1E930` -> `0x00D1EF98` (`DoAction(chieftain, 22, nullptr, &prop(ab_idx))`).
- Added `COOP_FIX("tribe.guest_consequence", "BUG-1564")` in `tribe.cpp` / `tribe_host.cpp` / `tribe_logic.h`: routes guest consequence ability activations over `MSG_TRIBE_CMD` (`kCmdConsequenceAbility = 6`), validates sender's own `history::player_card` (Cell/Creature) + cooldown + tribe chieftain on the host, executes natively via `SporeCoop::Ability_Use`, and reports `cons_ab_tx`/`cons_ab_rx`/`cons_ab_ran`/`cons_ab_denied` in `coop.tribe`.
- Added read-only `npc::pack_species_of(uint8_t player)` in `npc.h` / `npc.cpp` and `COOP_FIX("rivals.tribe_pack_pets", "BUG-1564")` in `tribe_rivals.cpp` / `tribe_rivals_logic.h`: snapshots each player's Creature pack species (deduplicated, clamped to 3), preserves them across `Creature -> Tribe` world change, spawns matching real domesticated pets once per player per Tribe world via `cCreatureAnimal::Create` + `DomesticateAnimal`, and reports `pack_pets_spawned` in `coop.tribe rivals status`.

## Verification
- Build & unit/lint suites: `make -j$(nproc)`, `make unit-cpp unit-net unit-lint`, `python3 tools/tests/test_lint.py` all PASS (`tribe_rivals_tests`: card-to-ability validation, cooldowns, pack-pet deduplication/clamping/idempotency).
- Wine lab ON/OFF twin on lane 4 (`20261125-175002`):
  - `rivals_tribe_abilities_pets` **PASS** (**OK**, 33 s, 0 host faults, 0 guest faults: Traps 52 ran `ran=1`, non-matching Refreshing Storm 53 denied `denied=1`, Fireworks 56 ran `ran=2`, 3 distinct wild species spawned as real domesticated pets `dom_count=3`, idempotent across ticks).
  - `rivals_tribe_abilities_pets_off` (`SPORE_FIX_OFF=tribe.guest_consequence,rivals.tribe_pack_pets`) **FAIL** as expected (57 s: `tx=0`, `ran=0`, `dom_count=0`, `pack_pets_spawned=0`).
- Wine lab regressions on lane 4 (`20261125-175143`): `rivals_tribe` **PASS** (**OK**, 36 s), `guest_befriend_tribe` **PASS** (**OK**, 77 s).
