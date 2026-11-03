---
id: BUG-0375
title: Creature: a guest playing its own creature never gets the brain level's health (max HP stays at level 0)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-01
source: player reports 2026-11-01 (host marks 13:39:07, 13:48:17)
role: guest
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w30-creature-hp
commits: d62e750
tests: tools/tests/coop_creature_brain_hp.py (suite creature_brain_hp)
signature:
docs: docs/re/creature.md#creature-max-hit-points
related: BUG-0373, BUG-0345
---

## Report
Host marks (0.7.3.3, Creature): "after growing brain more my friend didnt got extra HP i have 29 he has
only 14", 9 minutes later "another growth and my friend still has only 14 hp but i have 44".

## Root cause
Observed (logs, Ghidra, lane 9). The guest's own game really had 14/14 at every brain level (its
`trace:` lines), the host 10, 15, 19, 29, 44. Max hit points (creature vt 0x7C, 0xC04170) add the
brain level's health (10 15 25 40 ... by level) only to creatures of the world's player species
(cSpeciesManager +0xA4); the guest plays its own creation, which is another species in its own game
(the world's player species stays the host's), so it got level 0's 10 + its species health 4.
The brain level itself was the same on both machines (shared DNA, CRG_BrainUpgrade on both).
What the host showed for the guest: its puppet was pinned to the puppet's own full health
(BUG-0373), also level 0's.

## Fix
`creature.own_brain_hp` (creature_hp.cpp, hook of 0xC04170): the own avatar (`+0xB58 & 0x200`) of a
species other than the player species gets `CreatureLevelHealth(level)` + species health, the player
species' rule. The host shows it through BUG-0373's fix. Off: level 0's health (0.7.3.3).

## Verification
`coop_creature_brain_hp.py`: the guest plays its own creature (checked: another species than the
world's), the host gains 250 DNA, both games reach brain level 1 (captions closed with Enter); the
guest's own max health rises by the host's step (16 -> 21, host 10 -> 15) and each game shows the
other's new max on its puppet.
* FAIL with `SPORE_FIX_OFF=creature.own_brain_hp`: out/lab/20261101-080931-fix-w30-creature-hp (the guest's own max 16.0 -> 16.0 at level 1, host +5.0).
* PASS: out/lab/20261101-080343-fix-w30-creature-hp (with the other two tests), out/lab/20261101-081255-fix-w30-creature-hp (warm, with the regression tests creature, team, creature_desync, npc_run_hp, puppet_walk: 8/8).

## History
- 2026-11-01: opened from the host's report (0.7.3.3); reproduced and fixed on lane 9 (fix/w30-creature-hp).
- 2026-11-02: released in 0.7.3.4.
- 2026-11-03: an older direct message (2026-10-25..11-01, version not given): after evolving the brain in Creature only the host gets more HP; also 'no extra HP for each evolution piece (10 HP instead of 25)'. Fixed in 0.7.3.4 for the brain level; if the player had 0.7.3.4 or newer, the per-part health is a second path (BUG-0418).
