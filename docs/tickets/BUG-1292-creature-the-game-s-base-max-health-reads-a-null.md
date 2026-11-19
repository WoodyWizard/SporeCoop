---
id: BUG-1292
title: Creature: the game's base max health reads a null species and faults at 0xC04252 (fork BUG-9157)
status: fixed
kind: crash
stage: Creature
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9157
role: guest
versions: 0.7.15.2
fixed_in: unreleased
branch: fix/w56-general
commits: 287beb5e
tests: unit (npc_tests test_max_hp_null_species)
signature: 0xC04252 (cCreatureAnimal_BaseMaxHitPoints+0xE2)
docs: docs/re/creature.md#creature-max-hp-null-species
related:
switch: creature.max_hp_species
---

## Report
Found by the Lineages fork (PR #196), its BUG-9157: a guest that joined while the host was dead and pressed OK on its
death caption faulted twice at 0xC04252 (cCreatureAnimal_BaseMaxHitPoints 0xC04170 +0xE2) reading species +0x56C with
the species (+0xB20) null, a herd update on the stack. Our creature_hp.cpp h_BaseMaxHp called the native unguarded for
such a creature (own_base() returns false for a null species). No report of the signature in ours yet (latent).

## Root cause
Observed (disassembly of GOG 2024, objdump 0xC04170..0xC0429B, docs/re/creature.md#creature-max-hp-null-species): the
base reads species +0x56C with no null check in an adventure (0xC0419D), for the player species (a null species when
PlayerSpecies is null too) and for another species with no herd health (herd +0x1674 +0x154 > 0.0) and no tuning
(+0xE84) at 0xC04252. Inferred (the fork's runtime): a creature without a species meets the herd update after the
death caption.

## Fix
creature_hp.cpp h_BaseMaxHp (and the coop.hp diagnostic): a creature without a species on one of those paths
(max_hp_rule.h, the branches of the native) gets its max health (+0x5D4, at least 1) without the native call. Switch
`coop.fix creature.max_hp_species` (off: the native as it is). Test command `coop.hp nullspecies` (the own avatar
without species, herd and tuning for one vt 0x7C call, then put back).

## Verification
- Unit: npc_tests test_max_hp_null_species PASS; npc_tests --nofix FAIL (4 checks: the model faults at 0xC0419D /
  0xC04252).
- Lab (first 0.7.16 night run, rel/0.7.16 c7699dca, run dirs in the rel/0.7.16 worktree's out/lab/):
  PASS max_hp_species, out/lab/20261119-104136 (`coop.hp nullspecies` on the guest: the guard answered, max 10.0; the
  avatar has its species again and lives with 10/10).
  FAIL twin (SPORE_FIX_OFF=creature.max_hp_species), out/lab/20261119-105127, for the right reason: the guest's game
  crashed at SporeCoop::cCreatureAnimal_BaseMaxHitPoints+0xE2 (0x00C04252, data 0x56C), the fault of the report.
  The natural trigger (a join while the host is dead, then the death caption's OK) is still not reproduced on demand.

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on fix/w56-general (WP-F1); found by the Lineages fork (PR #196), its BUG-9157.
- 2026-11-19: lab verified (fix/w57-sync): PASS and FAIL twin (run dirs above).
