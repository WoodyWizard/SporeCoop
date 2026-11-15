---
id: BUG-9223
title: Creature: a guest's own editor spend is given back (DNA put back after the mating editor and cocoon)
status: fixed
kind: bug
stage: creature
reported: 2026-11-15
source: owner play test 2026-11-15 (BigTexas)
role: guest
versions: 0.7.14.1-lineages58-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 95378ac3
tests: none yet (built; the owner's session was running)
signature:
docs:
related:
---

## Report
Owner play test 2026-11-15 (host Pandize on TITAN's Steam Spore, guest BigTexas over the internet, 58-dev): "BigTexas went into editor with 14 DNA, added an Eye for 5 DNA, then went back ingame, still said 14 DNA, mated again, and possibly still had 14 DNA."

## Root cause
Inferred from the code: own_dna_tick (BUG-9165) put the guest's recorded DNA back whenever the Creature stage epoch changed, taking that for a reload of the host's world; the trip through the mating call's own editor and its cocoon raises the epoch too (a mode outside the editor's keep list), so the DNA from before the editor came back.

## Fix
- The put-back runs only after a load of a received copy of the host's world (world::shared_loads, counted where the guest loads it), once the loaded copy is open (its epoch moved on); the first sight of a world still restores its note. Switch creature.own_dna_after_load.

## Verification
no-fail-run: built only; to be checked on TITAN + the MSI after the owner's play session

## History
- 2026-11-15: opened.
