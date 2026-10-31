---
id: BUG-0341
title: Creature: a Steam guest crashes at 0x441F3B (model bake, null property list) every time it loads the host's Creature world
status: open
kind: crash
stage: Creature
reported: 2026-10-31
source: player reports 2026-10-31 (guest zip, Steam build, 3 crashes)
role: guest
versions: 0.7.3.1
fixed_in:
branch:
commits:
tests:
signature: 3010a413:SporeApp.exe+0x41F3B read 0x00000000 (dc04aee5 0x441F3B, FUN_00441910+0x62B, model bake: this+0xC property list null)
docs:
related: BUG-0344, BUG-0200, BUG-0027
---

## Report
Two players on 0.7.3.1: the host on GOG 2024 (Windows), the guest on the Steam build (3010a413,
"the GOG 2024 code" under SteamStub, Windows 11, loaded relocated at 0x00810000, the module list shows
d3dx9_27.dll at 0x00400000; Steam overlay present). The host left Cell for Creature ("entered the Creature
stage: everybody follows"), the guest received the world and crashed while loading it, 0.3 s
after "Loading". The guest rejoined twice; both times the game crashed at the same place 0.25 s
into the load of the host's Creature world. Three crashes in 3 minutes, no Creature play possible.

* crash 1 (game mode LoadGame): `0xC0000005 at SporeApp.exe+0x41F3B data=0x00000000`, main thread.
* crashes 2 and 3 (game mode Creature, frame ~1080 of a fresh start): the same address.

## Root cause
Observed (crash reports, minidumps, Ghidra on dc04aee5; absolute addresses of the report minus
0x410000 = dc04aee5 addresses):
* The fault is `mov edx,[eax]` at 0x441F3B in FUN_00441910 (a 23 KB function that builds a baked
  model from an editor model): `eax = this+0xC` is null and the code calls its vtable +0x1C with
  property id 0x9069C7C8. `this+0xC` is the property list the function takes from
  `IBakeManager::Get()` vtable +0x58 (or keeps from the model) earlier in the same function.
* Chain (frame pointers, rebased): FUN_00441910+0x62B <- FUN_00451440+0x1F4 <- FUN_004ae780+0xE9
  (per model part) <- FUN_0046dd10+0xC1 (EditorModel::Load, then the bake) <- cEditor::
  LoadCreatureData+0x395 <- FUN_004d4f70+0x3D <- FUN_004df740+0xA5 (species load) <-
  cSpeciesManager::GetSpeciesProfile 0x4DF5C0+0x169, i.e. the first GetSpeciesProfile of a species
  whose model is not built yet.
* Who asked for the species: crash 1: the game itself, FUN_00c05ea0+0x19 (a cCreatureAnimal
  vtable method of the save reader: reads the creature's species key at +0xB28 and resolves it),
  so a creature in the host's save. Crashes 2 and 3: coop.dll 0.7.3.1 (rebuilt from the tag,
  llvm-symbolizer; recovered from the stack of the minidump): `on_frame` -> `drive_puppet`
  (coop.cpp:435) -> `modes::creature_spawn` (modes.cpp:49) -> `creations::player_species(0)`
  (creations.cpp:1170) -> `game::species_by_key` -> GetSpeciesProfile: the host's puppet, built
  from the host's chosen own creature (`player 0 creature = 7C60ED12 ... (2 records)`).
* Both paths bake a creature the host brought: in that session the host's own avatar had just
  been swapped to an own creature from its choices.ini (BUG-0344) and the world was saved for
  the share right after the swap, so the save and the puppet both name that creature.
* The host's game (GOG 2024, all content) loads the same world and creature without a fault. The
  lab (GOG 2024 on every lane) never saw 0x441F3B.
* SporeKit's relocation handling is right: the log has `base=0x00810000 (preferred 0x00400000)`,
  symbols are rebased (hooks worked, all frames name the right dc04aee5 functions after the
  0x410000 delta), the code hash matched dc04aee5. The relocation is not the cause. Only the frame
  names of the report are misleading (nearest preceding symbol: "GetAnimationID+0x127D" is
  FUN_004d4f70, "cCreatureAnimal_GrowTo+0xF89" is FUN_00c05ea0), see BUG-0346.

Inferred (not verified), most likely first:
1. The guest's game lacks content a part of that creature needs. Steam sells "Creepy & Cute" as
   its own DLC (GOG ships it, `bp1content/BoosterPack_01.package`); a rigblock whose prop file is
   not there leaves the bake without its property list. SporeKit's file check does not look at
   `bp1content` and knows no sizes for Steam, so the report cannot tell.
2. The creature comes from the host's My Creations, not from the world (BUG-0344); its records
   reach the guest as a 2-record bundle (and as a 3-record "own creation of this world" under a
   0x7D... key), while the save names it by the host's key, which on the guest is free or is one
   of the guest's own creations (docs/known-issues.md "the host's own creations on the guest").
   A key that resolves to a partial record set would fit a model without its properties.

## Fix
Not done yet (needs lab runs). Plan:
* Reproduce on GOG lanes 5/6: (a) guest lane without `bp1content/BoosterPack_01.package` (moved
  aside in the lane copy only), host creature with a Creepy & Cute part, host enters Creature,
  guest loads; (b) both lanes complete, host with an own creature from choices.ini (BUG-0344).
  The one that crashes at 0x441F3B names the cause; (a) also without co-op (solo load of the save)
  tells a game limit from a mod bug.
* If (a): SporeKit reports the content packs (bp1content, EP1) in the log and the report; the
  handshake carries them; a guest missing a pack the host has gets a clear message at the join, and
  a creation with a part that is not in this game's resources is never baked here (check the
  rigblocks, docs/re/creations.md section 7, before `species_by_key` / SetSpecies; fall back to
  the local species like a creation without records). The world's own species cannot be swapped
  (rewriting the save lost herds, BUG-0200), so a missing pack must stop the join, not crash.
* If (b): fix BUG-0344 (the host keeps the world's species), and make the shared world never name
  a My Creations creature the guest cannot resolve.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-31: opened from the guest's report zip (three crash reports with minidumps and logs) and the host's zip (logs of the same session).
