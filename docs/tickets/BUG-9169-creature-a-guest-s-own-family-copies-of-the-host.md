---
id: BUG-9169
title: Creature: a guest's own family (copies of the host's members) wears the host's key of its species: no mate icon
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, the MSI as a guest of the TITAN host 2026-11-13: coop.evolve mates with the switch off and on)
signature:
docs: docs/re/creature.md#creature-migration-own-nests
related: BUG-9161 BUG-9160 BUG-9135 BUG-9168
switch: npc.own_family_species
---

## Report
Owner (2026-11-12, the MSI as a guest): the nestmate that answered the mating call showed the happy face on its card, not the mate icon ("I think that is a symptom of the real issue").

## Root cause
Observed on the MSI (2026-11-13): its creature is its own creation 2920408F (profile 49C52CD0); the host registers it as 7CE8822F and the host's members of the MSI's nest are of that key, so their copies on the MSI were of 7CE8822F (profile 49785220): another species for the game. BUG-9161 found the game offers a mate (CreatureClickAction class 9) only of the creature's own species profile. Also: copies of the host's own family (NF_FAMILY) went into the guest's avatar herd, its own nest, a rule from the shared-species days ("5C505F08 (copy) ... species 7D3670E8" in the MSI's herd).

## Fix
npc.own_family_species (on), guests in Creature: every second the copies whose key is the host's key of this player's own creature (creations::player_key of itself, or its local key) get the own creature's species profile (Simulator::cCreatureAnimal::SetSpecies, the copy's age kept); the host's family copies go to their nest herd, not the avatar's, while the guest plays a creature of its own (creations::plays_own_creature). `coop.evolve mates` lists the copies too ("(copy)"); `coop.npc` counts own_family.

## Verification
no-fail-run: manual on the MSI (no Windows lab lane). Switch off: the MSI at its own nest, its family copy "species 49785220/7CE8822F" with its own creature "49C52CD0/2920408F". Switch on: the same copy "species 49C52CD0/2920408F", own_family=1, 0 faults. Not verified yet: the mate icon (class 9) during a mating call on a guest; the copies are made adult (age 1) on the guest, the host's first member of a new nest is a baby.

## History
- 2026-11-13: opened.
- 2026-11-13: the species conversion removed with BUG-9180 (one key: the copies arrive with the own species); own_family_tick keeps the nest-herd join (BUG-9175).
