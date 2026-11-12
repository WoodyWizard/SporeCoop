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
