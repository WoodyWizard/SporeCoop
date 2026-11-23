# Galactic Adventures (adventures, kScenario 0x1654C10)

What is known about the adventure mode. Our own code only shows echo puppets in adventures (docs/07-modes.md); most facts
below come from the GA fork and are not verified by us yet.

## Facts from the GA fork (2026-11-10, plan/w47-gaforks)
<a id="adventure-ga-fork"></a>

Source: jwhans0n/SporeCoopGA, branch `sync/upstream-0.7.8-main` (docs/re/adventure.md, adventure-world.md,
build-dc04.md, data/symbols/overrides_ga_dc04aee5.sym with 202 entries and overrides_world_dc04aee5.sym with 19) and
`sync/upstream-0.7.8-editor` (overrides_editor*_dc04aee5.sym, 199 entries). All for GOG 2024 dc04aee5. **From a fork,
not verified by us** (`?`); the fork marks its entries "verified" from static disassembly (prologue, ret N, call sites).
Co-op adventures as a feature: BUG-0793; the shared editor: BUG-0863.

* CORRECTION (2026-11-10, plan/w47-gaforks, from a fork, not verified by us): `data/symbols/march2017_dc04aee5.sym`
  line `Simulator::cScenarioData::GetMarkerForObject=0x00F3D620` has the name reversed. 0xF3D620 is
  **GetObjectForMarker** (its avatar-marker branch compares data +0x8C and returns the noun manager's avatar);
  **GetMarkerForObject is 0xF3DB10** (its avatar-object branch returns data +0x8C). Both thiscall ret 4. The same file's
  `cScenarioData_GetMarkerForObject=0x00F3DB10` is right. We use neither yet.
* CORRECTION (2026-11-10, plan/w47-gaforks, from a fork, not verified by us): `march2017_dc04aee5.sym`
  `Simulator::cScenarioClass::IsConsumible=0x00F27730` is a different function; IsConsumible is **0xF25D50** (reads the
  first act +0x4A4 and compares with -1).
* ? The resource manager pointer used by App is 0x15FD894 (getter 0x67DC90); the alternate resource singleton getter
  0x8DE4B0 reads 0x1667AA0. DatabasePackedFile ctor 0x8DA2A0 thiscall(path16*, allocator) ret 8 (scenario Read calls it
  at 0xF2FD6D, Write at 0xF25E8A). Herd enable 0xC6AE80 thiscall(bool) ret 4 writes +0x218.
* ? Time of day: TimeOfDay_GetObject 0x1021110 returns the **active planet**; cTimeOfDay::SetTimeOfDay 0xBC3070
  thiscall(float time, Vector3* position) ret 8; helper 0xBC2DC0 computes the planet orientation from time and position,
  then SetOrientation and 0x1021630. So our MSG_WORLD_TIME quaternion depends on the host's position and is not a clock
  (BUG-0789).
* ? Scenario entry: ResetRequest 0xEF1910 cdecl, request at 0x15ACCC8 (adventure key +0, auxiliary key +0xC, captain key
  +0x18, entry kind +0x24). An adventure object: +0x18 its marker, +0x1C its class, +0x24 its runtime id; marker +0x228
  the placement id. Actor identity across games = (scenarioMarkerID, hostRuntimeID).
* ? Spatial cast: primary Cast vt +0x0C with type 0x01186577; combatant cast type 0x013F94D4; Cast(0xCE9F6639) returns
  the base creature (0xC0B7D0).
* ? Avatar controls: AvatarTickAI 0xC04020 thiscall(creature, float) ret 4 (called from Update at 0xC0B015 for the
  avatar); MoveAvatarWASD 0xD32330 thiscall(inputStrategy) plain ret; MoveAvatarToCursor 0xD31A70 thiscall(strategy,
  bool, bool) ret 8.
* ? Items and goals (overrides_world): PickUpItem 0xC15B20 thiscall(creature, item, slot, bool) ret 0xC (slots +0xE40 /
  +0xE44, stride 24); DropItem 0xC15EB0 ret 0xC; play-mode OnPickUp 0xF1AED0 ret 8, OnDrop 0xF1B060 ret 8, CountGoal
  0xF1ACF0 ret 0xC (goal +4 count, required +0x30), OnBefriend 0xF1D310, OnAlly 0xF1D3E0 (ret 4); posse PosseGet
  0xD53980 cdecl (singleton 0x169E5B8), PosseRecruit 0xD542E0 thiscall(manager, creature) ret 4; CreatureSocialMode
  0xC0E800 (named in our CreatureFriendly note): in Scenario relation 3 -> 2, 4 -> 1, 1/2/5 -> 0, 6 -> 4.
* ? Explosion entry F24730 cdecl(powerup, attacker combatant): radius class-act +0x498, amount +0x49C. Stock crate
  0x93512140 (powerup kind 15, amount 0), barrel 0xA0EBD95A (kind 15, amount 10).
* ? Editor (from `sync/upstream-0.7.8-editor`): edit history global 0x016065D8, StartHistory 0xF45810, CommitHistory
  0xF45920, Undo 0xF464B0, Redo 0xF464C0; CreateMarker 0xF44210, CloneMarker 0xF44460, DeleteMarker 0xF428A0; AddAct
  0xF448E0, DeleteAct 0xF440B0; SetCaptainAsset 0xF42420 (0xF40980 is a posse append, not a captain setter); CanSave
  0xF41110; NativePreview 0xEF12D0. Acts are 0x534-byte rows at resource +0x23C (1..8, up to 3 goals of 0x188 at +0x84);
  captain at resource +0x130. Complexity ratio = max(sum(count*weight) / MaxComplexity, (markers+1) / 1024), MaxComplexity
  float at 0x16C8944 (10.0), the 1024 limits at 0xF44267 / 0xF444DE / 0xF46326.
* ? Memory: big adventures reach about 1955 / 2047 MiB and then crash on a null allocation before a native write at
  0x9AC8E0 (Large Address Aware idea: BUG-0862).

## The mod's saves and quit message in an adventure (2026-11-12, fix/w48-saves)
<a id="adventure-mod-saves"></a>

* BUG-0854: `world::save_game` and `world::post_quit_to_menu` (coop.save, coop.menu, the quit before a load) refuse
  in kScenario (`world.adventure_saves`); the player leaves an adventure through its own menu. Shares are never made
  in an adventure (the adapter owns its worlds) and a load from an adventure goes through load_now.
* The tutorial adventure from the galaxy menu: `tools/tests/adventure_start.py` (clicks for 1440x1080). The menu's
  intro hides the menu for more than its 1300 frames at a high frame rate: the script now waits for the menu's
  "Galactic Adventures" entry and the end of the intro first.

## Two players in one adventure: the play mode, acts, the sky (2026-11-12, fix/w48-small)
<a id="adventure-two-captains"></a>

BUG-0845, BUG-0789. GOG 2024, objdump of SporeApp.exe and lab lane 2 (`tools/tests/coop_adventure_duo.py`, the tutorial
adventure "The Arena of Oorama" from the galaxy menu).
* ✔ (disassembly) `cScenarioPlayMode::JumpToAct` 0xF1F3C0 (`SporeCoop::ScenarioPlay_JumpToAct`) thiscall(play mode, int
  act) `ret 4`: returns unless the play state +0x90 is 3 and act < the act count (0xF3BCD0 on cScenarioMode +0x74);
  then SetCurrentAct 0xF1EE70 and 0xF1DD10 per goal. The play mode is `App::cScenarioMode::ptr` (0x16C7AA4) **+0x78**
  (the fork's "play controller"), its current act **+0xB8**. The game's own next / previous act handler at 0xF0EC6A calls
  JumpToAct with +0xB8 + 1 / - 1.
* ✔ (runtime) Right after the start (`adventure_start.py`: Scenario and an avatar) the play state is **1** for as long as
  nobody acts (60 s seen): the adventure's intro card waits for the player. Enter makes it 3 (playing). JumpToAct does
  nothing in state 1. `coop.adventure act 1` (outside the frame) then starts act 1 of the tutorial ("Trials of Combat":
  the gates open, Skex and Thorns come in), state 3, act 1, no fault (one game, lab lane 2).
* ✔ (runtime) The sky: both games of a session in the tutorial adventure had the same sky (same clock, same spawn);
  the host's captain put 60 m away moved the host's sky by 0.52 degrees (the fork: 4.95 degrees for 63 m on its
  adventure). With the legacy MSG_WORLD_TIME applied (`wsync.tod` and `wsync.adventure_sky` off) the standing guest's
  sky was the host's, 0.0 degrees apart (lab 20261112-122031). With `wsync.tod` on (since BUG-0579, the default) the
  packet is not applied by a guest and the wsync sky row is not in play in kScenario, so the default build already kept
  each captain's own sky; `wsync.adventure_sky` also stops the host's send and the legacy apply in an adventure.
* ✔ (runtime) Each game plays its own copy of an adventure: the scenario resource key (cScenarioMode +0x74 -> +0x10,
  instance +8 / group +0x10) differs per play (the tutorial: 6FCBCE96, E6F78525, 288BE48A, group 408A0000; one key was
  the host's in one run and the guest's in another), so the Adventure adapter's place ids differ and the partners are
  "in another world": no puppet in an adventure by default (BUG-0845). `coop.adventure place <hex>` (tests) forces one
  place id: then each game has the partner's captain as a creature puppet, and act changes (the game's own and
  JumpToAct) ran with it in both orders of entry without a fault (lab 20261112-124436, 20261112-125719).
* CORRECTION (2026-11-18, fix/w53-advorn): ~ superseded: the key is the same on both games in most runs (BF0CE0D0 in
  8 of 11 adventure_duo runs), so two players in the same tutorial share a place id without any command; and the
  puppets of the forced runs were clones of the own captain in the avatar's herd, which the act start skips. A puppet
  from the partner's Creature-stage creation (after a Creature session) faults at 0xC0714E: see #adventure-puppet-herd.

## A puppet outside the avatar's herd crashes the adventure's start (2026-11-18, fix/w53-advorn)
<a id="adventure-puppet-herd"></a>

BUG-1254 (players' video: the tutorial adventure "Adventure Town" after a Creature session; the guest's game crashed
about 3 s after the host's game showed the guest's puppet). GOG 2024, objdump and lab lane 9 / 7
(`tools/tests/coop_adventure_tutorial.py`).
* ✔ (runtime) The adventure's resource key is the same on both games in most lab runs: BF0CE0D0/408A0000 for host and
  guest in 8 of 11 adventure_duo runs found in the lab folders (the other 3: 288BE48A / 5292926C, 288BE48A / 6FCBCE96,
  6FCBCE96 / E6F78525, the early runs of #adventure-two-captains), and on both games of this ticket's runs. So two players starting the same tutorial get the same place id without
  any command: the "each game plays its own copy, no puppet by default" above holds only for the key, not the place.
* ✔ (runtime) After a Creature session each game has the partner's creation (`creations::player_species`), so the
  Adventure adapter (the Creature adapter's spawn) built the puppet from the partner's Creature-stage creature
  (065CBE70 on the fixture, scale 1.08-1.12 against the captains' 3.0) in a herd of its own (`game::create_herd`).
  The adventure_duo test never had a creation (it joins at the galaxy menu): its puppet was the fallback, a clone of
  the own captain in the avatar's herd.
* ✔ (runtime, both games) Right after the puppet was made (the load's 2.3 s hitch, "Alice appeared in the world"),
  `cScenarioPlayMode::Initialize` -> `SetCurrentAct` faulted at **0xC0714E** (data 0x1, esi = the puppet), the fork's
  signature of BUG-0845 (SporeApp+0x80714E). Under Wine the game's handler caught it: the play state stayed **0** for
  good (the intro card never came, Enter did nothing). The players' guest (Windows) crashed.
* ✔ (disassembly) SetCurrentAct 0xF1EE70 runs a pass (0xF1E5F0) over every creature (noun 0x18EB45E): a creature whose
  herd (+0x1674) is not the avatar's herd (cGameNounManager +0x58) gets `Creature_ScenarioActSetup` 0xC070D0. In
  kScenario it looks up the creature's scenario object (`cGameData_ScenarioObject` 0xB18470: cGameData +0x1C, -1 =
  none) and its class's act row ([obj+0x70] + act * 0x4E0); with no object the row is 0 and +1 / +2 are read
  unchecked. Any creature the adventure did not place crashes the start of every act unless it is in the avatar's herd.
* ✔ (runtime) The captains' species keys are made per game: one run had host 2927BAE4 / guest 2927BAE2, another the
  same key 2927BD73 on both games for two different captains (a green one and a robot, key frames of lab
  20261118-131729). A partner's captain key means nothing in the other game (BUG-1262).
* Fix `adventure.puppet_captain` (modes.cpp scenario_spawn / scenario_drive): an adventure's puppet is made in the
  avatar's herd, from this game's own captain species (the stand-in for the partner's captain), at the captain's size
  (PlayerState body: the captain's scale, 3.0 in the tutorial, sent in kScenario too, x 1.15 as the Creature puppets).
* Lab only: a game that loaded before the galaxy menu's start-up video ended showed no main menu panel when it came
  back to the menu (BUG-1260, fixed by world.menu_intro_return: docs/re/space.md#space-menu-intro-state); the test sent
  the video's end message first (`coop.menuintro`) until then.

## Where an adventure's captain comes from (2026-11-23, feat/w74-captain)
<a id="adventure-captain-copy"></a>

BUG-1262. GOG 2024, Ghidra (static). Adds to #adventure-puppet-herd ("the captains' species keys are made per game").
* ✔ The galaxy menu's Galactic Adventures launcher (vtable 0x147ED9C): choosing an adventure (0xDFFD70) reads its
  "captain locked" flag; locked: launcher +0xA4 = the adventure's captain (likely resource +0x130), no copy; not locked
  (the tutorial): +0x7C = 1 (a copy), +0x7F = 1 (a name). Play (SporeCoop::GAMenu_Play 0xDFBD40) copies the picked
  captain with 0x6462E0 (a new key of this profile, EditorSaves.package, metadata tag 0x4178B8E8) unless it is a copy
  already; the request's captain (+0x18 of 0x15ACCC8) is +0xA4. From Space (0xFDF5E0) the request takes cEmpire +0xC4;
  cScenarioMode's enter 0xEF1D20 then calls SetCaptainAsset 0xF42420 (resource +0x130) after the adventure loads.
* So each game's adventure captain is a creation in its own EditorSaves.package, readable by records_from_game; a
  partner's key names another creation here. creations_captain.cpp sends it (MSG_CREATION kind 11) and the puppet takes
  the copy (docs/re/creations.md#creations-captain-0717).

## Adventure creatures and the Creature NPC proxies (2026-11-23, feat/w83-advnpc)
<a id="adventure-npc-proxies"></a>

BUG-1474 (plan 0.7.17 item 6, Adventures part; owner question 6). GOG 2024, Ghidra (static, our project
`out/ghidra-2024`); no runtime this time (no lab by day). Question: can the Creature stage's NPC proxy layer (npc.cpp:
the host streams its NPCs, a guest removes its own and shows AI-less proxies, a guest's hit goes to the host) make the
host's adventure enemies the guest's? Answer: **no, not as it is** (the reasons below are static ✔ or likely ~); a
shared adventure needs its own layer that keeps the guest's adventure objects. Symbols: the `feat/w83-advnpc` entries
of data/symbols/overrides_dc04aee5.sym.

**How an adventure places and drives its creatures**
* ✔ (static) Markers: the adventure resource (cScenarioMode +0x74 -> +0x10) holds its markers at +0x2C10 (stride
  0x238, end +0x2C14, count +0x2C24); marker +0 is its class, +4 position, +0x10 orientation, +0x20 scale, +0x228
  the placement id. `ScenarioData_InstantiateMarker` 0xF3F8C0 makes a marker's object (CreateObjectInstance 0xEEDD40)
  and writes **cGameData +0x18 = the marker, +0x1C = its class**: only such objects are the adventure's
  (`cGameData_ScenarioObject` 0xB18470 reads +0x1C).
* ✔ (static) The scenario data keeps two hash maps of raw object pointers: GetObjectForMarker 0xF3D620 (marker +0x228
  -> object; the avatar's marker data +0x8C -> the avatar) and GetMarkerForObject 0xF3DB10 (object -> marker index).
  This confirms the fork's CORRECTION of #adventure-ga-fork (march2017's `Simulator::cScenarioData::GetMarkerForObject`
  at 0xF3D620 is GetObjectForMarker) with our own decompile.
* ✔ (static) Per act a class has a row of 0x4E0 bytes at class +0x70 + act * 0x4E0: +2 "out" (the act setup kills it:
  health 1 and a 2.0 hit, 0xF195D0), +0x48C the act's health (health scales by row(act) / row(act - 1) at an act
  change), +0x4A4 (< 0: the class is not in this act), +0x4AC the attitude 1..5 (combatant +0x5FC bits 0x10000000,
  0x40000000, 0x80000000, 0x200000, 0x20000000) unless +0x4C8 (then a behaviour block at +0x4CC), +0x4B8 == 1 one more
  bit 0x20000, +0x484 into creature +0x32C.
* ✔ (static) An act change (SetCurrentAct 0xF1EE70, thiscall(play, act, bool) ret 8): writes +0xB8, then
  RemoveInvisibleClasses 0xF1ABE0 (every marker object: a spawner object whose class is not in this act is destroyed
  with DestroyInstance, a visible one gets an "EP1_powerup_spawnerReincarnate" powerup, 0xF237B0), then
  `ScenarioPlay_SetupActGoals` 0xF1EBD0 (the goal vector, below), then the act pass `ScenarioPlay_ActSetupPass`
  0xF1E580 (the 0xF1E5F0 of #adventure-puppet-herd is its creature loop): Creature_ScenarioActSetup 0xC070D0 for every
  creature not in the avatar's herd and the act health 0xF195D0 for every combatant, creature and building.
* ~ (static, likely) Waves: spawner powerups tick in every game (`ScenarioSpawner_Tick` 0xF24240 from the powerup
  update 0xF24C40): they count the objects they made (map 0x16C8318) and, on their timer (+ row +0x4A4), make a new
  one (`ScenarioSpawner_Spawn` 0xF21D30 -> CreateObjectInstance with the class and the act ->
  `ScenarioObject_ActSetup` 0xF20180, which runs 0xC070D0 and 0xF195D0 on it). So each game makes its own waves and
  its own replacements; the exact count rule (when a missing object is made again) is `?`.
* ✔ (static) A creature's adventure behaviour reads its class's act row through `Creature_ScenarioActRow` 0xC048B0
  (Update's 0xC148B0, many behaviour tree nodes) and its attitude through `Creature_ScenarioAttitude` 0xC04930 (row
  +0x4AC, **2 for a creature without a scenario object**). The AI still runs through NPCTickAI (cCreatureAnimal::Update
  calls it for every non-avatar; our h_NPCTickAI skips puppets and proxies).
* ✔ (static) Goals: `ScenarioPlay_SetupActGoals` copies the act's goal rows (stride 0x188 at 0xF3BD00(act) +0x84) into
  play +0x64 / +0x68 (stride 0x1AC): goal +0x24 kind, **+0x28 the target class** (not an object), +0x30 required; a
  kind 8 / kind 5 goal credits the objects already there at once (`ScenarioCreditGoal` 0xF1ACF0, ret 0xC, returns
  unless play +0x90 is 3). An object of a goal's class made during an act gets the goal's marker (0xF1A6D0).
* ✔ (static) The end: `ScenarioPlay_SetState` 0xF1A9C0 (ret 4); cCreatureAnimal::TakeDamage calls it with 6 (failure)
  at 0xC07989 while the state is 3, on the captain's death.

**What npc.cpp's proxy path would do in kScenario** (today `npc_stage()` is Creature, Tribe and Civ only: an adventure
runs no NPC sync, each game its own creatures, as since 0.7.12)
1. ✔ (static, the same pass as BUG-1254's runtime fault) A proxy is made with Create in the species' nest herd or a
   herd of ours (`proxy_herd`), so it has no scenario object (+0x1C -1) and is outside the avatar's herd: the next act
   change's pass runs 0xC070D0 on it and faults at **0xC0714E** (row 0, as the BUG-1254 puppet did on both games).
   Every adventure with an act after the proxies appear breaks this way.
2. ~ (static, likely) Putting the proxies into the avatar's herd (the BUG-1254 cure for the partner's captain) avoids
   the fault but makes them the captain's own family: the pass skips their act setup and their attitude reads 2.
   ? Whether the guest's captain can attack (and the posse treats as enemies) creatures of its own herd: not checked;
   either way they would not be the adventure's (no class, so point 3 holds for them too).
3. ✔ (static) The guest's update filter removes every creature outside the avatar's herd and puppets: in an adventure
   these are the adventure's own objects (marker and class, in the marker maps, targets of the guest's goals by class,
   counted by its spawners). The guest's defeat goals then have no target of their class (a proxy has no class: it
   never counts), and ~ (likely, as the fork saw "80 unpaired, 56 at 0 hp after 3 minutes") its spawners make new
   ones that the filter removes again, frame after frame.
4. ✔ (static) The guest's acts, goals and end stay its own: a host kill reaches the guest's proxy only (no class, no
   goal credit), so the guest's run cannot advance with the host's, and its own act changes re-make the creatures the
   filter removed (RemoveInvisibleClasses, 0xF3F8C0).
5. ~ (likely) The host side (host_tick's candidates, ai_target in Creature only, MSG_DAMAGE to a puppet) is usable as it is:
   the fork's E-B lets `ai_target` run in kScenario for partner bodies.

**What a shared adventure needs instead** (the fork's working design, out/research/lineages-systems.md section E, now
backed by the static facts above): keep the guest's own adventure objects (they carry marker, class, act rows, goal
targets and spawner bookkeeping) and **pair** each host creature with the guest's creature of the same marker and class
(the fork: object id = FNV-1a over marker +0x18 and class +0x1C, nearest unpaired within 12 m), skip only their
NPCTickAI on the guest, drive them with the puppet driver, mirror the host's deaths through the game's Die, forward
the guest's hits to the host (npc.cpp's MSG_DAMAGE_REQ idea), remove the guest's unpaired leftovers after 5 s, and run
the goals, act changes and the end as one host journal (ScenarioCreditGoal 0xF1ACF0, SetState 0xF1A9C0). That is the
fork's E-A (shared run, M) plus E-B (host-run creatures, L), with runtime RE first (the pairing, the death through the
behaviour tree, the political id +0x30 of #adventure-ga-fork). npc.cpp's id and lifecycle layer (spawn a proxy,
remove the local one) does not carry over; its driving, damage request and old-peer gates do.

Verdict for 0.7.17 (provisional owner choice of question 6): the proxies do not work in an adventure, so no
`adventure.shared_npcs` setting in 0.7.17; the feature goes to 0.8 with co-op adventures (BUG-0793,
docs/11-rivals.md), proposal in BUG-1474.
