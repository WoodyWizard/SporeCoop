---
id: BUG-9157
title: Crash on respawn: base max hit points read a null species (C04252)
status: open
kind: crash
stage: creature
reported: 2026-11-12
source: test
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, two games on the MSI 2026-11-12)
signature: 0xC0000005 at 0xC04252 (BaseMaxHitPoints +0xE2) read of 0x56C, edi = species (+0xB20) null
docs: docs/re/creature.md#creature-max-hp-null-species
related: BUG-0375 BUG-0418
switch: creature.max_hp_species
---

## Report
QA 2026-11-12, two games on the MSI (Creature, Samuth.spo): the guest arrived in the world dead (the death caption up); its OK crashed the guest twice (crash-20261112-201509, crash-20261112-202143).

## Root cause
The native base max health (cCreatureAnimal_BaseMaxHitPoints 0xC04170, through the mod's pass-through hook creature_hp h_GetMaxHp:189) takes the else branch for a creature with no herd health and no tuning (+0xE84) and reads the species profile +0xB20 +0x56C without a check; the species was null (edi = 0). Reached from a herd update (game::h_HerdUpdate in the stack) right after the caption's OK. Which creature has no species is not known (inferred: the hatchling being made, or the old body).

## Fix
creature.max_hp_species (on): h_GetMaxHp returns the creature's current max (or 1) for a creature without a species instead of calling the native base. Off: as before (the crash).

## Verification
FAIL: twice without the guard, the same steps (join dead, OK on the caption) crashed the guest. PASS: with the guard the same steps respawned the guest at full health (31 / 31), 0 faults. no-fail-run for the lab: a manual two-game run.

## History
- 2026-11-12: opened.
