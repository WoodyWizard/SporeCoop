---
id: BUG-9361
title: A partner's Siren Song does not charm the host's animals
status: fixed
kind: bug
stage: creature
reported: 2026-11-23
source: owner
role: 
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: f81fc9e6
tests: tools/qa_win (manual, the MSI: coop.prules roar 32 on the guest, coop.prules slots 2 1 25 on the host)
signature:
docs: docs/re/creature.md#creature-siren-relay
related: BUG-0501, BUG-9250
---

## Report
Owner 2026-11-23 (5FB, "it should be all abilities working"); known since BUG-0501 (docs/re/creature.md#creature-roar-relay:
"Not covered: the siren song (0x32) of a guest"). A partner's Siren Song ran in its own game only, on proxies without
AI: the host's animals were never charmed.

## Root cause
As the roar (BUG-0501): the song is the player's consequence ability; the behaviour tree's node walks the creatures
of the game it runs in. Its calls read from the node's 0x32 branch (0xD8E942..0xD8EB1F, Ghidra on the Steam dump, the
function's start is the mod's hook jump: read past it with fasm).

## Fix
The partner sends its song as the roar (K_ROAR with type 0x32, range and seconds); the host's game makes the node's
calls on its creatures around the partner's puppet (player_rules.cpp siren_one). Switch `coop.fix creature.siren_relay
off`: the song stays in the partner's game.

## Verification
The MSI (TITAN's galaxy server; the MSIHost-NoShare- game hosting Samuth's Creature era, the MSIGuest-NoShare- game its
guest, whose creature has the roar, the song and the flock: coop.abilities 11, 12, 13). The guest's `coop.prules roar
32` (the node's first tick, as the ability button starts it), the host's `coop.prules slots 2 1 25` (its creatures of
another species within 25 m of the guest's puppet holding the song's goal slot, the game's own goal state):
- before: 0 of 1; switch off: 0 of 1 (nothing sent); switch on: 1 of 1 ("siren ... played=1 charmed=1").
~ One creature in range only; its behaviour was not watched (the puppet stood 55 m from the host's camera).
no-fail-run: the FAIL is the switch off on the same build.

## History
- 2026-11-23: opened.
- 2026-11-23: fixed on feat/lineages-alpha, seen off and on on the MSI.
