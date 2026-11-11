---
id: BUG-0819
title: Creations: the BUG-0200 species alias also runs while the guest plays its own world (suspected)
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creations.md
related: BUG-0200 BUG-0341 BUG-0452
---

## Report
No player report. In a connected guest's own save, NPC nests whose species instance collides with one of the host's own creations show the host's creature; suspected: herd members spawned meanwhile may carry the alias key into that save.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): creations.cpp alias_tick (2085-2117) runs whenever the guest is connected; creations.cpp has no world gate; alias_skip (2070-2083) spares only the avatar's key, the guest's own key and the world's player species. Inferred from the code comment at 2068: SetSpecies with an aliased profile writes the profile's 0x7D key into +0xB28, which a later launch has no package for (the BUG-0341 / BUG-0452 model-bake class). Confidence: medium.

## Fix
Not fixed yet.

## Fix idea
Run the alias only while the guest plays the host's world; undo it when the guest goes elsewhere, as at the session end.

## How to test
nest_species fixture (host creation 290229B9) and a guest save with a species of the same instance; the connected guest loads its own save: `coop.creations speciesof 290229B9` must stay the guest's own key (fix) vs 7D... (main); save, restart solo, check the herd's +0xB28.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
