---
id: BUG-0341
title: Creature: a Steam guest crashes at 0x441F3B (model bake, null property list) every time it loads the host's Creature world
status: fixed
kind: crash
stage: Creature
reported: 2026-10-31
source: player reports 2026-10-31 (guest zip, Steam build, 3 crashes)
role: guest
versions: 0.7.3.1
fixed_in: 0.7.3.2
branch: fix/w22-reports
commits: 2a4420a
tests: guest_missing_pack (tools/tests/coop_guest_missing_pack.py)
signature: 3010a413:SporeApp.exe+0x41F3B read 0x00000000 (dc04aee5 0x441F3B, FUN_00441910+0x62B, model bake: this+0xC property list null)
docs: docs/re/creations.md#creations-content-packs
related: BUG-0344, BUG-0200, BUG-0027, BUG-0354
switch: creations.missing_parts
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

Reproduced (2026-10-31, lanes 5/6, `coop_guest_missing_pack.py`): cause 1 below. A guest whose game
lacks `bp1content/BoosterPack_01.package` faults at exactly 0x441F3B (data 0) 0.5 s into the load of a
host world whose creature has ONE Creepy & Cute part (the fixture's Tiger Horse with one part swapped
for 0x17A60A7F); under Wine the fault is caught and the load hangs (Windows: the crash). The host and a
guest with the pack: no fault. Cause 2 alone (a host playing its choices.ini creature, all parts
present) did not crash a guest (lane 5, by hand). The second batch of zips (fix/w22-reports2,
out/w22-evidence.md) shows all 6 guest crashes while the host played its choices.ini creature: BUG-0344
is what put the old creature (most likely the one with the C&C part) into the shared save.

Inferred before the repro (kept for the record), most likely first:
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
`creations.missing_parts` (on by default; `coop.fix creations.missing_parts off` = 0.7.3.1):
* `mods/coop/content_packs.h`: the 119 part ids only Creepy & Cute has (from the GOG 2024 files'
  index). `creations::model_parts` reads the part ids of a creature model (bem), see
  docs/re/creations.md#creations-content-packs; `have_creepy_cute()` asks the resource manager for
  the first part once (logged: `content: Creepy & Cute parts pack present / NOT present`).
* On a game without the pack: a player's creature with such a part is not registered (its puppet
  falls back as for an unknown creation, the player is told); the host's own creation of its world
  with such a part is not registered and the world is not loaded: "cannot load <host>'s world:
  their creature '<name>' uses parts of the Creepy & Cute parts pack, which this game does not have.
  Install that pack, or the host replaces those parts" (`world.cpp`). An NPC species of the host's
  world with such parts is not registered either.
* Together with BUG-0344 (the host no longer brings an old choices.ini creature into its world) a
  world whose creatures have no C&C parts loads normally.
Not covered: parts of other packs (none known), a creature with a C&C part that reaches the save
without being one of the host's own creations (a Sporepedia download: the guest's game gets its
records from the host only on demand, after the load).

## Verification
* FAIL (fix off, `SPORE_FIX_OFF=creations.missing_parts`, Carol switched off too): out/w22/missing_pack_fail1.txt
  of the worktree (direct run, lanes 5+6): Carol faults at 0x441F3B and stays in LoadGame. The same
  without any fix code: out/w22/missing_pack_1.txt.
* PASS: out/w22/missing_pack_pass1.txt: Carol is told, stays in the galaxy menu, 0 faults; Bob (with the
  pack) plays.
* Lab (Carol on lane 6 via SPORE_FREE_LANE): FAIL 20261031-131550 (fix off: fault 0x441F3B, stuck in
  LoadGame), PASS 20261031-131835 (told, galaxy menu, 0 faults).

## History
- 2026-10-31: opened from the guest's report zip (three crash reports with minidumps and logs) and the host's zip (logs of the same session).
- 2026-10-31: reproduced on GOG lanes 5/6 (missing pack -> 0x441F3B); fixed on fix/w22-reports (creations.missing_parts).
- 2026-10-31 (fix/w24-parts): the owner chose substitution over refusal: BUG-0354 shows the pack's parts as similar base parts on a game without it (`creations.part_substitute`); the refusal stays as the fallback for a part without a substitute.
- 2026-11-01: released in 0.7.3.2.
