---
id: BUG-9360
title: Crash loading a host's world made from its own old save: SetSpecies with no animation (0x00A920E0)
status: fixed
kind: crash
stage: creature
reported: 2026-11-23
source: qa
role: 
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: ffd01832
tests: tools/qa_win (manual, the MSI: coop.attachtest with game.attach_no_model off / on; a Vandh private planet joined)
signature: 0xC0000005 at 0x00A920E0 SporeCoop::cLocomotiveObject_SetDesiredSpeed+0xB70, read 0x17C, < cCreatureBase::PlayAnimation+0x1C80 < cCreatureAnimal::SetSpecies+0xB8 < cGamePersistenceManager::LoadGame
docs: docs/re/creature.md#creature-attach-no-model
related: BUG-9215
---

## Report
The MSI, 2026-11-23 (8N A test, TITAN's galaxy server, both games of the build 7f3d2bd0..8N): the host game (profile
MSISpaceStart) loaded its own old local save Vandh.spo (from the 2026-11-15 stage tour, Creature stage) and entered it
as a new galaxy planet (a private one); its party partner (profile MSIGuest) joined it from the galaxy menu: the
partner got the host's five own creations ("the host's own creation ... is ... here") and the 4.6 MB world, then
crashed in the world's load: crash-20261123-221524 (out/crash_8n/, not in git), the game's main thread.

## Root cause
Ghidra (GOG 2024 addresses; the Steam game ran 0x180000 lower: 0xA920E0 is 0xC120E0): the fault is
SporeCoop::Creature_AttachToSocket 0xC120B0+0x30, reading [creature+0xB54]+0x17C with +0xB54 (the animated model)
null. The chain: the world's load reads each creature (cCreatureAnimal Read 0xC05EA0) and calls SetSpecies(profile, 0,
true); when the species' counts (+0x590 / +0x594) match the creature's hold slots (+0xE40..+0xE44, 0x24 bytes each),
SetSpecies calls 0xC14010, which puts each held object back on its socket (0xC120B0) and so needs the animated model.
A creature of the host's world that carried something (a slot with a socket) had no model yet at that point. Inferred,
not seen: such creatures are the host's own creations the guest had just installed (their models not built when the
load reads them); it depends on what the world's creatures carry at the save, which is why the same join did not
crash again (two more joins of the same Vandh on 2026-11-24 loaded).

## Fix
gamefixes.cpp attach_guard: a hook on SporeCoop::Creature_AttachToSocket; a creature without its animated model
(+0xB54 null) gets no attach (false, as for a slot without a socket; 0xC14010 goes on with the next slot), logged
("its held object stays off socket ..."). Switch `coop.fix game.attach_no_model off`: the game's own code.
Test command `coop.attachtest`: the attach on a stand-in creature without a model.

## Verification
The MSI (the build with the guard; TITAN's galaxy server):
- PASS (switch on): `coop.attachtest` "attach of a creature without a model: false (guard on, skipped 1)", the log line
  "creature ... has no animated model yet: its held object stays off socket 0 (BUG-9360)", the game went on. Then the
  report's case: the host loaded its old save Vandh.spo, made it the party's private planet, the guest joined it from
  the galaxy: the world loaded, "MSIGuest plays its own creature 'Yankin'", no fault (the guard was not needed there
  this time).
- FAIL (switch off, the same build): `coop.attachtest` crashed the game: crash-20261124-114712 (out/crash_9360/, not in
  git) "access violation at 0x011D20E0 SporeCoop::Creature_AttachToSocket+0x30 ... read of address 0x0000017C": the
  report's fault (+0x30, read 0x17C).
- Not reproduced: the load crash itself (the same Vandh join twice without the guard's need, 2026-11-24); the FAIL is the
  fault site with a stand-in creature, not the whole load.
no-fail-run: the load crash did not come again; the FAIL run is the fault site (coop.attachtest, switch off).

## History
- 2026-11-23: opened.
- 2026-11-24: guarded on feat/lineages-alpha (a held object's attach without a model), FAIL and PASS at the fault site on the MSI.
