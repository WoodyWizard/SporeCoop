# SporeCoop 0.7.16 plan (draft 2026-11-18): every player's own species in every stage

**Status: draft for the owner (branch plan/0.7.16, from main 4d19482f). Nothing of it is built yet; the questions at
the end wait for the owner.**

Goal (the owner): in the normal co-op mode (not Rivals) every player has its own species and its own look in every
stage. Today Cell, Creature (the own creature from My Creations, docs/09-coop-design.md 9.4.1, creations.cpp) and Space
(the ship) have it; in Tribe and Civilization every member, citizen, vehicle and building is the host world's (the
tribe and the civilization are shared; BUG-1156..1158 synced the host's outfit and designs and refused a guest's own
outfitter and design editors in the host's world), and in Cell the wild kin are all the host's cell (BUG-1257).

The research behind this plan (with evidence, GOG 2024):

| Area | Where | Main finding |
|---|---|---|
| Tribe | docs/re/tribe.md#tribe-two-species | one player tribe holds members of two species: a member's species is its own field (+0xB20 / +0xB28), its model and size come from it; nothing of the game puts it back (40 s); a save keeps it per member (probe `tribe_species_probe`, lab 20261118-132622) |
| Civ | docs/re/civ.md#civ-own-species | a city's citizens are of `City_Species` (the civilization's key +0x268), re-set per city on every sync: one hook answers per city; vehicles and buildings take a design per object (SetModelKey, stats follow) |
| Cell | docs/re/cell.md#cell-kin-species | the wild kin are the cells of a kind-5 resource made in the population pass; the mod already hooks the model key and a guest's mirror carries the host's key |
| Correction | docs/re/creations.md#creations-species-setters | 0xD5BFA0 (2016) / 0xD5BF50 (2024) is the city citizens' sync, not "every tribe member after the editor" |
| Symbols | data/symbols/overrides_dc04aee5.sym | 13 new entries (cTribe_SpeciesSlot, Citizen_Init, City_Species, CityCitizens_Sync, cCreatureBase_PostRead, ...) |

## Feasibility in one table

| Item | Feasible | How | Cost | Main risk |
|---|---|---|---|---|
| 1. Tribe members of the guest's species | **yes, shown at runtime** | SetSpecies per guest-owned member on every game; the save hook puts the tribe's key back while a save is written | M (members) + M (own outfitter) | the save: a member's key reaches the host's save without the strip (it does, by the probe) |
| 2. Civ citizens, vehicles, buildings | **yes, by Ghidra** (citizens untested at runtime; per-object designs proven by BUG-0647 / 0670 / 1157) | hook `City_Species` per city owner; SetModelKey per guest-owned vehicle / building with the culture key put back for saves | M (citizens) + L (designs and own editors) | the shared capital (one species per city); `Civ_SetDesign` re-models every vehicle of a kind |
| 3. Cell kin of every player's species | **yes, by Ghidra + existing hooks** | the existing model-key hook answers a partner's cell key for kind-5 cells inside the population pass on the host | S | the partner's cell model data built on the host (BUG-0455's check covers an unknown key) |
| 4. Stage transitions keep each player's line | **yes, by design** (the game carries only the host's line: the Tribe -> Civ end records the tribe's slot) | the mod keeps a per-player species line (creature, tribe outfit, Civ outfit, designs) and applies it after each arrival; own editors at the arrival (BUG-1240's pattern) | M | own editors at the Tribe -> Civ arrival (City Hall, vehicle) on a guest |

## Item 1: Tribe

**Mechanism (docs/re/tribe.md#tribe-two-species).** cTribe +0x18CC is the tribe's species slot; a new member takes
it in `Citizen_Init` 0xC27330 (no species argument), babies too. After that the member's own profile (+0xB20, key
+0xB28) decides its model (`Creature_RebuildModel` 0xC21D50), size (`Citizen_ComputeScale` 0xC23E50, the species'
size normalised) and abilities (`Citizen_AbilityCount`); `Citizen_IsPlayerSpecies` answers true in Tribe whatever the
species. The outfitter's return 0xCDCC90 reloads the slot profile in place and rebuilds every member from its own
profile (a guest's member stays the guest's). Each game's member species is its own field (not sent); a save keeps it
per member (`cCreatureBase_PostRead` 0xC225D0) and a key the game lacks loads as the world's player species.

**Design.**
* Every game, for every member of the player tribe whose owner (tribe.cpp `owner_of`, `for_each_owned`) is a player
  with an own species line: `cCreatureAnimal::SetSpecies(member, profile(line key), age, keepState 1)` when its +0xB28
  differs. Triggers: the split at arrival, a new member (spawn, baby, a purchase), an owner change (gifts E1 / BUG-1154,
  a player leaving: back to the slot), a world load or rejoin, the host's outfitter return, and once a second as a
  check (cheap: a key compare). The apply waits for cinematics (BUG-1173) and is spread over frames (BUG-0318: a body
  is built over several frames).
* Saves: `savestrip::add_save_hooks`: at the save's start every member whose +0xB28 is not the slot's key gets the
  slot's key in +0xB28..+0xB30 (the three dwords only, no rebuild), put back at the end (the civ.host_buildings
  pattern). The host's save then holds the host's species only; a guest's own save the same.
* Keys: the guest's line key is its creation's shared key (creations.cpp: the same 0x7D...... id on every game, the
  bundle with png, `creations.bundle_png`), known before it is applied (`creations::key_known`, else wait).
* The guest's own outfitter (WP-T2): in "own" mode the guest's tribe card edit badge opens the outfitter (0x156276D1)
  for **its** species (the request built with its line key, as `coop.vote editor` builds requests), instead of the
  BUG-1158 refusal. On the guest the return 0xCDCC90 must not reload the slot (it would dress the host's members in
  the guest's outfit on the guest's screen): its third branch is skipped for our request; the new creation becomes the
  guest's tribe line, goes out as a bundle and every game applies it to the guest's members. The host's own outfitter
  stays as BUG-1156 synced it (the slot, the host's members).
* `creature.mode` = host (F5) keeps today's behaviour (everybody the host's species), as it does in Creature.

**Work packages.**
* **WP-T1 Members of the owner's species** (one agent, lanes 2): apply, triggers, the save hooks, the leave / rejoin
  paths, switch `tribe.own_species`. Test `coop_tribe_own_species.py` (tribe fixture, the guest with its own creation):
  on both games every guest-owned member's +0xB28 = the guest's key and the host's = the slot's, its model key (+0xB20
  +0x510) the guest creation's, the key frame of the village (two creatures, the visual check); a baby bought by the
  guest becomes its species within 2 s on both; a save of the host (a new slot) parsed: no key of ours and no guest key
  in it (as tribe_outfit checks); a rejoin brings the mix back. FAIL twin: `SPORE_FIX_OFF=tribe.own_species` (all
  members the slot's species). Plus the probe kept as a regression of the save finding.
* **WP-T2 The guest's own outfitter** (one agent, lanes 2; after WP-T1 and WP-0): the request with the guest's key,
  the guest-side return hook, the line update and broadcast; BUG-1158's tribe refusal only in host mode. Test with the
  players' route (real clicks, #creations-stage-designs: the hut, the card's badge, a body piece, Done): the guest's
  members wear the outfit on both games, the host's do not, the slot's key on the guest is still the host's. FAIL twin:
  the switch off (the refusal, 0.7.15). RE inside the package: how the card builds its request (the slot key; 0xCD4F90
  / 0xCD1D60 read cTribe_SpeciesSlot) and that our request's result reaches 0xCDCC90 on the guest only.

**Risks.** A missed strip puts a guest's key into the host's save: with the session on, it loads as the guest's
species; without it, as the host's (Ghidra, not run): the test parses the save. A member in an action during SetSpecies
(keepState 1: the probe's member was idle): the test changes the species of a member carrying food or a tool. The
roster portraits stay the tribe's picture for every member (cosmetic, question 3). Babies show the slot's look for a
frame or two before the swap. Abilities, size and health follow the guest's creature (balance, by design).

## Item 2: Civilization

**Mechanism (docs/re/civ.md#civ-own-species).** A city's citizens are a pool of the city near the camera;
`CityCitizens_Sync` 0xD5BF50 gives each the city's species `City_Species` 0xBD9F90 (the civilization's key +0x268)
and puts it back whenever it runs, so per-citizen SetSpecies does not hold; one hook of City_Species (5 callers, all
citizen code) answers per city. Vehicles and buildings carry a model key per object (+0xC4) and SetModelKey changes it
with the design's stats; the culture set is one per civilization; `Civ_SetDesign` re-models every vehicle of a
locomotion and purpose and every building of a type. The Tribe -> Civ end records the tribe's slot key as the Civ
species; the Civ outfitter's return writes the player species, the civilizations' keys and every tribe's slot.

**Design.**
* Citizens: hook `City_Species`: a city owned by a player with an own line answers that player's Civ species (its
  tribe line, or its Civ outfit once made). Every game knows the city owners (civ.cpp). An owner change (a gift, a
  capture by a guest's vehicles) calls the sync of that city once (0xAC8C10 / CityCitizens_Sync). Nothing is written
  into the civilization, nothing is sent per citizen, nothing reaches a save.
* Vehicles and buildings: a guest-owned vehicle or building gets SetModelKey(the guest's design for its design id)
  after it is made (a purchase through the host, a City Hall / house / factory / entertainment the guest builds),
  after `Civ_SetDesign` on the host's civilization (hook its end) and after a load; its culture key goes back into
  +0xC4 while a save is written (the civ.host_buildings save hooks). Mirrors on the guests use the same keys (the
  creations.host_designs path already re-keys bound vehicles).
* The guest's own Civ editors (WP-C3): in own mode the guest's design editors (buildings, land / sea / air vehicles)
  and the Civ outfitter open for its own line (BUG-1158's Civ refusal and the "your pick went back" undo only in host
  mode); the result goes to the guest's line, not the culture set; the Sporepedia pick in the planner likewise.

**Work packages.**
* **WP-C1 Citizens of the owner's species** (one agent, lanes 1-2): the City_Species hook, the owner-change resync,
  switch `civ.own_species`. RE first: a runtime check that the hook changes the citizens of a city (lane 4,
  civ_npc fixture: `coop.civ` gives a city to the guest, the camera at that city). Test `coop_civ_own_species.py`: on
  both games the citizens near the guest's city are of the guest's species (the citizen objects' +0xB28), the host's
  city's are the host's; key frames of both cities; FAIL twin.
* **WP-C2 Vehicles and buildings of the owner's designs** (one agent, lanes 2): re-key on make, after Civ_SetDesign,
  after a load; the save hooks for vehicles (buildings have them); switch `civ.own_designs`. Test: the guest's purchased
  land vehicle and its house show the guest's bem hash on both games (`creations::model_hash`, as civ_designs), the
  host's the host's, the vehicle's hit points the design's; a host save parsed: culture keys only. FAIL twin.
* **WP-C3 The guest's own Civ editors** (one agent, lanes 2; after WP-0, WP-C1, WP-C2): the editors' requests and
  returns on a guest in own mode, the line update. Test: the guest builds a house design in its building editor (real
  clicks, the route of #civ-culture-sync / tribe.md "the City Hall editor") and buys a vehicle of its own design: both
  games show them; the host's culture set unchanged.

**Risks.** The shared capital (fewer cities than players): one species per city, so its citizens stay the host's
(question 1). A Civ_SetDesign re-model of the host's civilization overwrites the guests' vehicles for a frame. A
vehicle's or building's key the game does not know on load (if the strip is missed) was never tried: the tests parse
the save. Captured NPC cities follow the captor's owner. The design stats change the balance (by design).

## Item 3: Cell kin

**Mechanism (docs/re/cell.md#cell-kin-species).** The kin are the cells of a kind-5 resource created inside the
population pass (PopulateTiles -> PopulateTile -> PopulateMarker -> ... -> CreateCellObject ->
GetModelKeyForCellResource, kind 5 = `S+0x10`); the avatar, its respawn, the mate and the rebuild's avatar are made
outside it. modes.cpp already hooks GetModelKeyForCellResource (forced keys, the BUG-0455 unknown-key check) and
cellsync hooks PopulateTiles; a guest's mirror is made with the key the host's state carries.

**Design.** On the host, while its population pass runs (a depth counter in h_PopTiles), a kind-5 answer (the result
equals `S+0x10`) is replaced by a partner's own cell key when the policy says so (question 4: alternate, or the
player whose view centre is nearest the new cell), only when `creations::key_known` for that key. Nothing else
changes: the guests draw those cells from the key. A guest's own pool while the host is away (BUG-0205) may do the
same with the host's key. Switch `cell.kin_species`.

**Work package.** **WP-L1 Cell kin of every player** (one agent, lane 1): the hook, the policy, the switch. Test
`coop_cell_kin_species.py` (cell fixture, the guest with its own cell creation, food at a level with kin, as
BUG-0637's `coop.pool species`): both models among the host's pool and among the guest's mirrors (the game's own cell
objects' +0xFC model key), the avatar of each player still its own, a key frame; after the guest's cell editor the
next kin made are of its new cell. FAIL twin: switch off (only the host's cell).

**Risks.** Small: a partner's model data built on the host on first use (BUG-0455 path); the kin's AI is read from
the resource (inferred, the partner's kin behave like the host's). Kin made before a guest's editor keep its old cell
until they leave the tiles (the host makes no rebuild for a guest's edit).

## Item 4: Stage transitions keep each player's line

**Mechanism.** Cell -> Creature has the own editor at the arrival (BUG-1240, `core.own_editor_arrival`). Creature ->
Tribe has no editor; the tribe's slot is the host's creature. Tribe -> Civ: `TribeGame_AdvanceProc` records the
tribe's slot key as the next species (docs/re/civ.md#civ-own-species), then the host's City Hall and vehicle editors
(TRG2CVG_PreEditor); BUG-1158 refuses the guest's. Civ -> Space: the ship editor is the guest's own already
(0x96B24187 allowed).

**Design: the species line (WP-0, shared by every package).** Per player: the creature (creations::mine(kCreature),
exists), the tribe outfit, the Civ outfit, and the design key per design id (buildings, vehicles). The host keeps the
table, sends it with the creations' bundles to every game (new players too), keeps it across rejoins in the
per-player store (player_store, E1 of 0.7.15), never in the game's save. Rules: entering Tribe the guest's tribe line
is its tribe outfit if it made one for this creature, else its creature; entering Civ its Civ species is its Civ
outfit, else its tribe line; its designs start empty (the host's culture keys until it makes its own). `creature.mode`
= host empties every guest's line (today's behaviour). A portable header with unit tests (the rules, the wire format,
the store row).

**Work packages.**
* **WP-0 The species line** (one agent, unit tests + lane 1): the table, the messages, the store, `coop.species` (print
  per player and stage), the rules above; no visible change by itself.
* **WP-X1 Own editors at the Tribe -> Civ arrival** (one agent, lanes 2-3; after WP-C3): as BUG-1240 for Cell ->
  Creature, the guest's City Hall and vehicle editors open after its arrival in Civ (or are offered), their results go
  to its line. Test: `coop_evo_screens.py`-style real clicks through the Tribe -> Civ change on two games (tribe fixture
  near the stage end): the guest's editor opens after it loaded, its City Hall / vehicle are its own design on both
  games, the citizens of its city its species.
* **WP-X2 The line through the stages** (one agent, lanes 2-3; last): one test from the creature fixture through
  Creature -> Tribe -> Civ with the guest's own creature: in Tribe its members, in Civ its city's citizens and its
  vehicle are its line, at each step on both games, with key frames (the campaign's segments, docs/06-testing.md).

## What can be shared with the Rivals plan (docs/11-rivals.md)

* **The species line (WP-0)** is the per-player half of Rivals' "Ownership" and "Perspective" rows (11.8) and of
  #rivals-own-designs (the owner's decision of 2026-11-13: own species, outfits, vehicles and buildings for every
  player in 0.8). Build it once here, Rivals reads it.
* **Per-object application by owner** (WP-T1, WP-C1, WP-C2) is what a Rivals player needs while its faction is still
  shared or mixed; in Rivals proper each player gets a tribe and a civilization of its own (R2), where the game itself
  has one species per faction (the tribe's slot via `cTribe_SetSpeciesSlot`, the civilization's +0x268 via
  `Civ_SetSpeciesKey`, its own culture set): those are saved fields, so the same save hooks (put the world's keys back
  while saving) apply. The research shows the native per-faction route exists, so "A tribe has one species in the game,
  so an own species per player needs RE first" (docs/11-rivals.md) is answered: one tribe can hold several species per
  member, and each tribe has its own slot.
* **Own editors on a guest** (WP-T2, WP-C3, WP-X1) are the first piece of R1 (the guest's UI acting on its own part
  of the world: its outfitter, its design editors) without swapping "the player"; Rivals' full R1 is still needed for
  cards, goals and the HUD.
* **Cell kin per player** (WP-L1) is Rivals' one shared tide pool (11.3.1) as it is.
* Not shared: Rivals' relations, influence, stage ends, the draft.

## Order and lanes

1. WP-0 (unit first), in parallel WP-L1 (independent, lane 1) and WP-C1's runtime check of the City_Species hook.
2. WP-T1 (lanes 2), WP-C1 (lanes 1-2), WP-C2 (lanes 2) in parallel.
3. WP-T2 and WP-C3 (lanes 2 each).
4. WP-X1, then WP-X2 (lanes 2-3).
5. The final check (the release rule), visual checks with key frames in every new test.

Every package: a switch per behaviour (FAIL twin), its tests on two games checking the game's own fields on the other
machine and a key frame, a save parse where it writes a field a save could see, knowledge in docs/re and the `.sym`.

## Questions for the owner

1. **The shared capital in Civ** (while there are fewer cities than players): the game has one species per city;
   should the capital's citizens stay the host's species (proposed), or alternate by some rule?
2. **A guest's members and units in the host's save**: proposed: the host's save holds the host's species and designs
   only (the guest's are put back while saving, as with every object of ours), so the host playing alone later sees its
   own species everywhere. Or should the host's world keep the guest's look (that needs the guest's creations in the
   host's world, BUG-0873's shared library)?
3. **The Tribe roster's portraits** show the tribe's species for every member (cosmetic, needs more RE to change):
   acceptable for 0.7.16?
4. **Cell kin**: half and half, by player count, or each player's kin near that player (the nearest view)?
5. **Gifts and owner changes**: a member or vehicle given to another player takes the receiver's species / design at
   once (proposed), or keeps the look it was born with?
6. **The switch**: `creature.mode` (own / host) already decides Cell and Creature; should it also decide Tribe and Civ
   (proposed), or a separate session setting (for example "Tribe and Civ: own species / host's species")?
7. **Scope of 0.7.16**: all four items, or Tribe + Cell first (WP-0, WP-T1, WP-T2, WP-L1) and Civ (WP-C1..C3, WP-X1,
   WP-X2) in 0.7.17?
8. **Space**: the guest's captain and crew species (cEmpire's species, the captain's portrait, BUG-1111) are the
   host's empire's today; in scope for this line, or later?
