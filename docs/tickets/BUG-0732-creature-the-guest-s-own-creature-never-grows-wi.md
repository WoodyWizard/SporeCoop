---
id: BUG-0732
title: Creature: the guest's own creature never grows with its brain upgrades (creature vt 0x80 scales only the world's species)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-08
source: BUG-0418 sibling list, K2b audit 2026-11-08
role: guest
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2b-content-b
commits: 8911e865, e896b4a8
tests: creature_grow, creature_grow_slow (coop_creature_grow.py)
signature:
docs: docs/re/creature.md#creature-player-species-callers
related: BUG-0418, BUG-0375, BUG-0733
---

## Report
K2b audit of the class BUG-0418: a guest playing its own creature never grew with its brain upgrades. The size rule creature vt 0x80 (0xC04B50) applies the avatar normalizing scale and the brain factor (0x150C8E4: 1.0 .. 1.5 by level) only to the world's player species (cSpeciesManager PlayerSpecies 0x4DF490), and the guest's own creature is another species in its own game.

## Root cause
Observed (lab 20261108-040526, `SPORE_FIX_OFF=creature.own_scale`, the guest playing its own Kirgle): at brain level 1 the host's creature went 1.0 -> 1.15 (the brain factor 1.1 and the level's normalizing scale), the guest's own stayed 1.0. Static: vt 0x80 is not cached; the brain upgrade 0xD3FCA0 gives the avatar SetScale(vt 0x80(0)); the guest's vt 0xD0 call at 0xC04B6F and the compare after 0xC04C5B both say "not the player species" (docs/re/creature.md#creature-player-species-callers).

## Fix
creature_hp.cpp (`creature.own_scale`, the BUG-0375 pattern of the own avatar): vt 0x80 of the own avatar of another species runs with PlayerSpecies answering the avatar's own species (a hook of 0x4DF490 that answers an override only while one is set) and puts back the normalizing global `g_AvatarNormalizingScale` 0x169E374 it recomputed (the world species' creatures keep theirs). `coop.fix creature.own_scale off`: a stranger's size (0.7.10). New symbols: cCreatureAnimal_ComputeScale, g_AvatarNormalizingScale, CreatureBrainUpgrade.

## Verification
FAIL: out/lab/20261108-040526 (`SPORE_FIX_OFF=creature.own_scale`: the guest x1.000, the host x1.150). PASS: out/lab/20261108-034334 (creature_grow: the guest x1.150 like the host), out/lab/20261108-041412 (the _slow twin at 20 fps over the player network).
Re-run on the merged branch fix/w46-k2b-content (the run dirs above were in the sub-worktrees fix/w46-k2b-content-a / -b, removed 2026-11-08; their ids stay as the record): FAIL out/lab/20261108-065817, PASS out/lab/20261108-054842.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): fixed, `creature.own_scale`; FAIL and PASS runs above.
- 2026-11-08: FAIL / PASS runs repeated on the merged K2b branch.
- 2026-11-08: released in 0.7.11.
