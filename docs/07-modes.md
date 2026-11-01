# 7. Co-op in every mode

Every frame the mod detects the game mode (`GetActiveModeID`) and picks an **adapter** for it
(`mods/coop/modes.{h,cpp}`). An adapter answers three questions:

1. What the local player controls (`local`) and how to describe it in `PlayerState`
   (position, orientation, velocity, health, flags - `fill`).
2. How to show a remote player (a "puppet": `spawn` / `drive` / `despawn`, `alive`).
3. Where the player is (`place`): players see each other only when both their **mode** and
   **place** match. By default the place is the current save's name (crc32 of the
   `CurrentGameName` string, 0x0167EF40). In space the place is a star or planet; in adventures
   it is the adventure key.
   After the live Creature -> Tribe change the game's name reads `snapshot.spo` until the next save:
   the planet read before it stays the place (BUG-0258, docs/re/tribe.md#tribe-snapshot-name).

`PlayerState` (protocol v2) carries `mode` and `world`, so the panel shows who is where:
`Bob [Space]`, `Bob [Adventure, other world]`.

## 7.1 Summary

| Mode | What the player has | Puppet seen by others | Place | Shared progress | Test |
|---|---|---|---|---|---|
| Cell | avatar cell (`cCellGame` pool) | cell with the avatar's model, invulnerable | save | - | `coop_mode.py --fixture cell` ✅ |
| Creature | avatar creature | creature in the avatar's pack | save | DNA, collected parts (the union, [creations-part-pickup](re/creations.md#creations-part-pickup)), host-side NPCs, damage via the host | `coop_two_instances.py` ✅ |
| Tribe | mouse pointer on the planet | name-tagged pointer + `coop ping` marks | save | tribe food | `--fixture tribe` ✅ |
| Civilization | mouse pointer on the planet | name-tagged pointer + marks | save | civilization money | `--fixture civ` ✅ |
| Space | player's ship (`cSimulatorPlayerUFO`) | "defender" ship, invulnerable | star (StarID) or planet (PlanetID) | empire money | `--fixture space` ✅ |
| Adventures | captain (avatar creature) | captain creature | adventure key | - | echo puppet ✅ |
| Editors, menus | - | puppets hidden | - | - | - |

## 7.2 World, saving and guest join

* The guest joins from the main menu (`coop join`). The host **saves the game** (with the same
  `kMsgSaveGame` 0x1CD20F0 message the menu button uses, via `MessagePost`), waits until the
  game's "saved" box shows up (the save is complete; at least 2.5 s, at most 10 s), confirms the
  box (it pauses the game) and sends the save folder. The guest puts it into a free slot and loads
  the planet the host named. One slot can contain several planets.
* **Which slot and planet the host sends** (`world.cpp current_slot`). A slot `Games\GameN` is a
  galaxy, not one game: every game the player starts on a free disc of the galaxy menu is one more
  `<planet>.spo` in the same folder (a fresh profile: a new Cell game next to an old Creature game,
  both in Game0; verified 2026-10-24 on GOG 2024; other `GameN` folders are co-op worlds received as
  a guest, fixtures, warm-reset slots). So the folder says nothing about the game being played; the
  planet does: `CurrentGameName` (string16 0x0167DF60 GOG 2024), set when a game is started from the
  wizard as well as when one is loaded ("SaveGame" in the galaxy menu). A new game has no `.spo` until
  its first save: the save before sharing writes it (Cell: `<planet>.spo`, the PlanetScripts and the
  galaxy files are rewritten; `lastSave.pld` keeps its old time, so its age says nothing).
  The host sends: (1) the folder the save database points to (`GetDatabaseByID(0x4729A47)` vt 0x28,
  where the game writes this game) when it holds `<planet>.spo`, (2) else the newest copy of
  `<planet>.spo` in any `GameN`, and names `<planet>` in the offer. When no file of the played planet
  exists (the save did not write it), it saves again (3 tries) and then tells the host; it never
  names another planet of the slot. Only from the galaxy menu (no game played) the newest slot and a
  planet in it go out. The guest loads exactly the named planet and nothing when it is missing.
* Planet names are whatever the player typed in the wizard ("Земля", "Bob's World"): the mod reads
  `CurrentGameName` as UTF-8, file names travel as UTF-8 and the load converts the name back to
  UTF-16. Until 0.7.2.1 non-ASCII letters became '_': the host did not find "_____.spo", sent the
  galaxy with another planet's name in it, and the guest loaded an older game of the host (player
  report 2026-10-24; the host "had to delete the old save"); the guest also built the load name from
  UTF-8 bytes, so such a planet never loaded. Test `coop_join_new_game.py`.
* A world (or `coop.load`) that arrives while a stage runs is loaded through the galaxy menu: the
  game's quit message 0x4715068 saves and leaves the stage, the save is loaded ~3 s after the menu
  is up (`world.cpp load_slot`). Loading straight from a stage crashed from Space (null active
  star, docs/re/space.md 8) and misbehaved from Cell; test `coop_stage_switch.py`.
* Space: the game only saves when off-planet. If the host is on a planet, the guest waits and the
  host sees "a player is waiting for the world: leave the planet to save" (since BUG-0382 every
  player gets a notice that the world follows when the host's ship leaves the planet, and the
  host's trace line ends with `share_wait=planet`). Forcing a save on a
  planet was tried and rejected: such a save loads with UI errors every frame (0xEBDD12)
  and a hang.
  The check is made when the save is posted; the game writes it up to ~1 s later, and a ship that
  landed in between made such a save too (BUG-0357, docs/re/space.md#space-share-save-on-planet):
  a share whose save was written on a planet is saved again.
* Adventures are not saves. No world is transferred; everyone launches the same adventure
  themselves. Puppets appear once the adventure keys match.
* A synchronous save (`MessageSend`) from the frame hook (inside `Present`) deadlocks the
  DXVK threads, so only `MessagePost` is used.

## 7.3 Cell

* `cCellGame` = `*0x016B4BE4`. Cell pool at `+0x1C` (data `+0`, object count `+0x10`,
  object size `+0x14` = 0x398), avatar index `+0x411C`.
  `cObjectPoolIndex` = slot (low 16 bits) | generation (high 16). An object is alive
  while its first field equals the full index (`GetIfNotDeleted` 0xB72290).
* The puppet is created via `CreateCellObject(query +0x40FC, pos, 0, avatar resource,
  level, 1, 1, true)`. The level comes from `GetCurrentAdvectInfo`: the avatar's own `mScale`
  is −1, and cells with −1 are not rendered. Every drive keeps it at the players' level as the game
  reads the avatar's −1 (GetScaleDifference(avatar, puppet) = 2): with the level of its creation
  the host's NPCs saw the partner as too small to hunt (docs/re/cell.md 9). The model is set by a hook on
  `GetModelKeyForCellResource`: during creation it returns the avatar's current `mModelKey`.
  Without the hook the "PlayerCreature" resource would produce the cell from the initial selection.
* Movement is driven through `mTargetPosition` with `mIsIdle = false`, just like the player's own cell.
  If it lags by more than 4 m, the position is set directly. Chasing and fleeing are disabled;
  the cell is invulnerable and always has 6 HP. On removal HP = 0 and opacity 0, and the game
  itself cleans the cell up.

## 7.4 Tribe and Civilization - "pointers"

The RTS stages have no single avatar, and everyone shares one world (the host's). The tribe,
cities and citizens therefore already exist on every machine. The mod shows **where each player is
currently working**:

* The pointer is the point on the planet under the mouse. The ray comes from `cViewer::GetCameraToPoint`
  and is intersected with a sphere of radius |position of the tribe/first city|.
* `coop ping` places a blinking `[ ! ] Name` mark for everyone for 6 s (`MSG_PING_MARK`;
  the host relays it to the others).
* Shared resources (`shared.cpp`, `MSG_RES`/`MSG_RES_ADD`). The total lives on the host; guests
  send their changes (earned or spent) and get the total back:
  * tribe food - sum of the food bundles in the tribe's `cGameBundleContainer` (+0x20C, vector +4,
    `float` amount per bundle +0x134; verified against `cTribe::GetTotalFood` 0xC8E850);
    `UpdateFoodVisuals` is called after a change;
  * civilization money - `cCivilization::mWealth` (+0x98) from `GetPlayerCivilization`;
  * space money - `cEmpire::mEmpireMoney` (+0xD0) from `GetPlayerEmpire`.
* Unit commands are not synchronized. Each player controls their own copy of the same tribe or
  civilization; only resources and marks are shared. Full unit sync is a separate large
  milestone (see 7.7).

## 7.5 Space

* Player ship: `cSimulatorPlayerUFO::Get()+0x40`. Parts of `cGameDataUFO`:
  `cLocomotiveObject` +0x34 (position +0x38, orientation +0x44, velocity +0x1FC),
  `cCombatant` +0x508 (HP +0x540, max +0x534, invulnerability +0x588).
* The puppet is created via `SpawnUFO(Defender=6, player's political ID, empire's mUFOKey)`.
  A "defender" ship is friendly to its own empire. Every frame the mod sets the position
  and orientation, as well as `mNextPosition`/`mDestination`/`mbAtDestination`; otherwise
  the ship's AI steers it towards its own goal.
* Place: `SpacePlayerData` (`*0x016DEA6C`, verified via `GetActiveStar`) → context
  (0 planet, 1 system, 2 galaxy). On a planet the place is the `PlanetID`, in a system the
  `StarID`. In the galaxy view puppets are hidden.

## 7.6 Phase transitions (who decides a phase is over)

* **The host decides.** Phase progress is shared (DNA, food, money) and is computed by the host's game.
  When the host meets the normal transition condition, the phase ends for everyone.
* The host goes through the editor and the transition, then sends the new world to guests on the
  next join or on `coop share`. While players are in different modes, puppets are hidden and the
  panel shows each player's mode (`Bob [Editor]`).
* Planned: a readiness panel ("Phase complete - ready: Alice ✔, Bob …", 60 s timeout,
  `coop next`) and blocking guests from advancing on their own.
* Space has no phase end. In adventures each player's act ends separately, and players
  meet within the same adventure.
* Rivals mode (designed, [chapter 11](11-rivals.md) 11.5): every player has its own condition;
  the first to meet it wins the stage and starts a catch-up timer for the others, then everybody
  moves on together (the host's game also when a guest won).

## 7.7 Not yet implemented

* Tribe and civilization unit sync (shared control). Needs host authority
  for `cCreatureCitizen::DoAction`/`cVehicle` (`MoveTo`, `SetTarget`) with network
  unit IDs based on political ID and index.
* Auto-launching the host's adventure on the guest. A function that starts an adventure
  by key has not been found yet: the menu goes through `kMsgSetGameModeByName` 0xE11333, and the
  adventure resource is read by `cScenarioResource::Read` 0xF2FC40 via the resource manager.
* Shared NPCs outside the Creature stage: cells, citizens and ships are simulated separately on each machine.

## The host's world: one rule for every module (2026-10-30, fix/w9-core)
<a id="modes-host-world"></a>

A guest can play another world of the host's stage while connected: (a) it joins from inside a
game of its own, (b) it loads one of its own saves, (c) the host loads another save of the same
stage, (d) a share failed or the guest has `auto_load_world=0`. Before 0.7.4 each module decided
this differently (BUG-0130, BUG-0231 / GitHub #165). Now there is one rule, `mods/coop/host_world.h`
(pure, unit tested in `tests/validate_tests.cpp` test_host_world), and every module asks it through
three functions in coop.cpp:

* `host_world_now()`: `NotGated` (not a connected guest; Space, adventures, menus, editors, loads;
  the host's state in another mode: follow_phase pulls the guest), `Same` (the host's latest state,
  or before the first one the world the host named in WELCOME, is this machine's world, not 0),
  `Other` (anything else in Cell, Creature, Tribe or Civ, including "no state and no WELCOME world"
  from an older host, and this machine's world 0). Recomputed on every call, never cached.
  `core.same_place off` makes it `NotGated` everywhere.
* `guest_elsewhere()`: `host_world_now() == Other`.
* `host_absent_for(Absent::Npc | Tribe | Civ)`: the host does not drive this module's shared
  objects on this guest now (away in the menu, an editor or a load: `g_host_away`; or elsewhere):
  the guest plays them itself. The npc part keeps BUG-0130's switch, Tribe and Civ use
  `core.world_gate`. A later "host away" change edits only the away part inside it.

`Welcome::world` (protocol unchanged, the field existed and was never set): the host fills it every
frame with its save CRC in a gated stage (`host_world::welcome_world`), the guest keeps it
(`Session::welcome_world()`, reset by `leave()`). A guest joining in the host's world is `Same` from
WELCOME on, so it no longer drops the host's Civ state, Cell knockouts or a build event in the round
trip before the host's first state.

What each module does while `Other`:

| Module | While elsewhere | Switch |
| --- | --- | --- |
| message gate (`gate_drops`) | drops DNA total (kept as `g_dna_pending`), time of day, Cell, Tribe, Civ state / build / alarm, species, social | `core.world_gate` (off: 0.7.3.0's predicate, no Civ messages) |
| npc, tribe, civ | `local_play()`: the guest's own AI and orders | `core.same_place` (npc), `core.world_gate` |
| civ `city_catch_up` | founds nothing (second guard: `local_play()`) | (behind the gate) |
| DNA (`dna_route.h` row 2) | a gain is applied in the guest's own world, nothing is sent; the host's total replaces it once the guest plays the host's world | `core.world_dna` |
| shared money / food | held (`SharedLedger::pause`): no sampling, no sending, host totals ignored; pending and in-flight changes are kept and go out after the first host total back in the host's world | `res.elsewhere` |
| Space | never gated here: squadron `with_host` and spacecoop `my_place` decide per star system | |

The host side: a share that fails after the save (`world::tick`, "cannot share the world") calls
`world::set_share_failed`; coop.cpp clears the guest's `world_sent` (and sets `needs_world` for a
share of its join) and follow_phase sends it again 30 s later, at most 3 times per phase and guest,
then the host is told to use `coop.share` (`core.share_retry`). The tag is the guest's
`join_serial`, so a newcomer that got the id of a guest who left is not retried for it. "Planet
file missing" is not retried (world.cpp saved 3 times already).

Limits: two different planets saved under the same name count as one world (a protocol change would
fix it). Rivals mode (docs/11-rivals.md 11.8, "Perspective" and "Per-player progress") builds on
this rule: it answers "whose world is this machine in", the modules decide what to share.
