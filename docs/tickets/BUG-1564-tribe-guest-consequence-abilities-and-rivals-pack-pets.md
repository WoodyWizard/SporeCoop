---
id: BUG-1564
title: Tribe: guest consequence abilities and Creature-to-Tribe pack pet transfer in Tribe Rivals
status: open
kind: feature
stage: Tribe
reported: 2026-11-25
source: 0.8.1 Rivals isolation audit
role: both
versions: 0.8.0.2
fixed_in:
branch: w081/tribe-consequence-pets
commits:
tests: rivals_tribe_abilities_pets, rivals_tribe_abilities_pets_off
signature:
docs: docs/11-rivals.md, docs/re/tribe.md
related: BUG-1548, BUG-1554, BUG-1557, BUG-1560
---

## Report
1. Tribe stage consequence abilities (earned from Cell and Creature consequence cards: Fire Bombs, Refreshing Storm, Flying Fish, Traps, Fireworks, etc.) are not routed from guests to the host for the guest's own tribe and own consequence cards.
2. On `Creature -> Tribe` transition in Tribe Rivals, each player's recruited Creature pack species are not carried over as domesticated pets at their own village (whereas solo Spore transfers pack creatures into village pets).

## Root cause
- `tribe.cpp` / `tribe_host.cpp` do not forward guest Tribe consequence ability activations or execute them against the sender's `cTribe` on the host.
- `tribe_rivals.cpp` takes over an NPC tribe for each guest without transferring the guest's Creature-stage pack species (`npc::pack_owners` / `creature_rivals`) into domesticated village pets.

## Fix
- Reverse-engineer and route guest Tribe consequence ability activations to the host (validated against the player's own consequence cards and tribe) behind `COOP_FIX("tribe.guest_consequence", "BUG-1564")`.
- Record each player's Creature pack species before Tribe entry and spawn matching domesticated pets at each player's village in Tribe Rivals behind `COOP_FIX("rivals.tribe_pack_pets", "BUG-1564")`.

## Verification
Pending implementation and Wine lab ON/OFF twin verification.
