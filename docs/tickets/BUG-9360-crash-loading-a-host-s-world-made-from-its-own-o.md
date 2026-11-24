---
id: BUG-9360
title: Crash loading a host's world made from its own old save: SetSpecies with no animation (0x00A920E0)
status: open
kind: crash
stage: creature
reported: 2026-11-23
source: qa
role: 
versions: 
fixed_in:
branch: feat/lineages-alpha
commits:
tests:
signature: 0xC0000005 at 0x00A920E0 SporeCoop::cLocomotiveObject_SetDesiredSpeed+0xB70, read 0x17C, < cCreatureBase::PlayAnimation+0x1C80 < cCreatureAnimal::SetSpecies+0xB8 < cGamePersistenceManager::LoadGame
docs: docs/known-issues.md#known-host-own-save-load-crash
related: BUG-9215
---

## Report
The MSI, 2026-11-23 (8N A test, TITAN's galaxy server, both games of the build 7f3d2bd0..8N): the host game (profile
MSISpaceStart) loaded its own old local save Vandh.spo (from the 2026-11-15 stage tour, Creature stage) and entered it
as a new galaxy planet (a private one); its party partner (profile MSIGuest) joined it from the galaxy menu: the
partner got the host's five own creations ("the host's own creation ... is ... here") and the 4.6 MB world, then
crashed in the world's load: crash-20261123-221524 (out/crash_8n/, not in git), the game's main thread.

## Root cause
Not analysed. The fault is in the game's own load (LoadGame -> cCreatureAnimal::SetSpecies -> PlayAnimation) reading
0x17C off a null pointer: a creature of the world whose species (or its animation data) the partner's game does not
have. The private planet's server rules do not touch the world transfer or the load (the same path as any new planet
made from a host's loaded world). Next: reproduce with a plain (not private) planet from the same save; list the
world's species against the creations sent.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
