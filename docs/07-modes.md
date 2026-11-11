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

## Message classes and the world filter of events (2026-11-07, fix/w46-k2a-state)
<a id="modes-message-classes"></a>

BUG-0420 (class: hits, events and world state applied without a relation / world filter), 0.7.11:

* Every `net::MsgType` has one class in `host_world.h` `msg_class()`: Session (applied wherever the guest plays: the
  session, chat, a partner's own creature, a guest's report to the host, the vote, MSG_RES held in shared.cpp),
  HostWorld (dropped at dispatch while the guest is elsewhere: the old hand list), Module (the module gates by place:
  npc, wobj, tribe / civ host_away, Space with_host, effects C.here) and Unknown (a type this build does not know:
  dropped while elsewhere, default deny, `core.msg_classes`). The switch has no default and
  `#pragma GCC diagnostic error "-Wswitch"`: a new MsgType that is not classified fails the build of coop.dll and of
  tests/validate_tests.cpp (BUG-0701).
* MSG_VOTE stays session-wide; its handler takes no vote that starts while the guest is elsewhere and opens no
  evolution editor at the end of one (`core.vote_world`, BUG-0702).
* The species table: a guest elsewhere forgets the host's last table instead of writing it into its own world every
  0.5 s (`social.world_gate`, BUG-0703).
* Relation filters: an NPC nation's vehicle (owner 0xFE) is no player of a joint siege (`civ.siege_players`,
  BUG-0704); Space hits between the players' empire and its own escorts are not relayed (`space.friendly_hits`,
  BUG-0705: the game itself applies no damage between ships of one empire, lab 20261107-222158, so this one is
  hygiene, not a visible fix).
* Tests: coop_world_gate_vote, coop_civ_siege_npc, coop_space_friendly; validate_tests (the classes).

## Session settings (2026-11-08, feat/w46-settings)
<a id="modes-session-settings"></a>

F3 of docs/plan-0.7.12.md, BUG-0762. One framework for the co-op mechanics the host can switch. Code:
`mods/coop/settings_core.h` (portable: the table, the rules, the wire and file formats; unit tests in
`tests/validate_tests.cpp` test_settings) and `mods/coop/settings.cpp` / `settings.h` (the game side).

**Declared once.** Every setting is one row of `settings::kDefs`: `key` (console, wire and file name, never renamed),
`group`, `label` (the lobby's text), `help`, the built-in default, the range, the scope, the `coop.ini [coop]` key that
gives a new world's default (or none) and `off_in_rivals`. The UI (F1) lists the rows by group; nothing else needs
to know them. A module reads a value with `settings::on(settings::kX)` / `settings::get(...)`: never `config_int`.

| Key | Group | Default | Scope | coop.ini key | Read in |
|---|---|---|---|---|---|
| `cell.regen` | Cell | on | live | - | cellsync.cpp symbiosis_tick (twin switch `settings.cell_regen`) |
| `all.partner_arrows` | All stages | on (off in Rivals) | live | - | find.cpp arrows_on (F2) |
| `all.dna_pace` | All stages | on | session | `dna_pace` | coop.cpp dna_pace_announce (BUG-0510) |
| `creature.npcs_target_partners` | Creature | on | session | `npcs_target_partners` | npc.cpp ai_target |
| `creature.pack_rescue` | Creature | on | session | `teamplay` | teamplay.cpp on_own_damage |
| `creature.pack_heal` | Creature | on | live | `teamplay` | teamplay.cpp tick |
| `creature.joint_hunt` | Creature | on | session | `teamplay` | teamplay.cpp on_player_hit |
| `all.name_tags` | All stages | on | live | - | coop.cpp on_overlay (puppets, tribe members, civ cities and vehicles) |
| `all.friendly_fire` | All stages | off | session | - | npc.cpp damage_filter / MSG_DAMAGE (Creature only) |
| `all.max_players` | All stages | 4 (2..4) | live | - | net/session.cpp HELLO (`Session::max_players`) |
| `all.late_join` | All stages | on | live | - | net/session.cpp HELLO (`Session::join_closed`) |
| `cell.knockout` | Cell | on | session | - | cellsync.cpp go_down |
| `cell.respawn_small` | Cell | on | session | - | cellsync.cpp downed_tick |
| `core.switch_vote` | Cell | on | session | - | coop.cpp h_CellToCreature, start_vote (arrival) |
| `creature.mode` | Creature | own (own / host) | session | - | creations.cpp mine, cycle; coop.cpp h_Submit, the offer; coop_ui.cpp |
| `core.editor_now` | Creature | on | session | - | coop.cpp h_Submit |
| `tribe.pincer` | Tribe | on | session | - | tribe.cpp h_CitizenTD |
| `tribe.chief_aura` | Tribe | on | session | - | tribe.cpp h_CitizenTD (+20 %, the chief keeps 30 %) |
| `tribe.duet` | Tribe | on | session | - | tribe.cpp duet_tick |
| `tribe.epic` | Tribe | on | session | - | tribe.cpp epic_tick, `coop.tribe epic` (`epic force` spawns anyway) |
| `civ.roles` | Civilization | on | session | - | civ.cpp role() (every role bonus) |
| `civ.joint_siege` | Civilization | on | session | - | civ.cpp h_AddCapture |
| `civ.superweapon_vote` | Civilization | on | session | - | civ.cpp h_FireSW, host_superweapon_request |
| `space.disable` | Space | on | session | - | spacecoop.cpp filter_own_damage |
| `space.trade`, `space.contracts`, `space.diplomacy`, `space.allies`, `space.finale`, `space.rivals` | Space | on | session | - | space_ext.cpp (tick, messages, overlay, page rows of the feature), space_finale h_Enter, space_diplomacy sharing() |

**Step 3 (BUG-0768, feat/w46-settings-all).** Every other mechanic of the plan's table is a row now; the default is
0.7.11's behaviour. One twin switch for all of them: `coop.fix settings.all off` (SPORE_FIX_OFF=settings.all) makes
every row from `all.name_tags` on read its built-in default whatever the host set (the tests' FAIL runs).
* `all.max_players` / `all.late_join`: the host's net session refuses a HELLO over the limit ("session is full (the
  host takes N players)", REJ_FULL) or of a new player while the host plays its world with late join off ("the host
  does not take new players after the start", REJ_STARTED = 4, an older guest shows the text). A player who was in
  this session (its name, kept since `host()`, or its rejoin token) always comes back.
* `cell.respawn_small` off: the knock-out ends after its 10 s in the game's own death (KillCell on the avatar,
  `ko_deaths=` in the cell status); `cell.knockout` off: no knock-out at all.
* `all.friendly_fire` on (Creature only): a player's hit on a partner's puppet (the game's TakeDamage, the damage
  filter) is sent to the host as MSG_DAMAGE with the victim's id; the host applies a hit on its own avatar or relays
  it to the victim guest (`npc friendly fire: pvp_out / pvp_in / pvp_relayed` in coop.status). The host's own hit on a
  guest's puppet goes the NPC hit's way. Cell, Tribe and Civ need more than a filter (the cell pair hook skips
  puppets, the tribe members and civ vehicles of all players share the player's tribe / political id): BUG-0899.
  Test command: `coop.team pvp <player id> <dmg>`.
* Space features: `space_ext.cpp` gives a feature that its setting switched off no tick, no messages (dropped), no
  overlay and no rows on the F9 Space page; `coop.space features` says "off (the session setting)".

**One creature for everyone (F5, BUG-0769).** `creature.mode` = host: on a guest `creations::mine()` returns the
host's creation of the kind (cell, creature: the host's newest bundle, else the world's species) whatever the guest
chose or recorded for the world, so the avatar swap and the BUG-0508 follow give every guest the host's creature and,
after each host editor, its new one in place. The guest's own editor requests (nest, mating call) are refused in
h_Submit (a feed line), the host's evolution opens at once with no vote and no offer, a vote end or an offer opens
no own editor on a guest, the co-op window shows no creation arrows and no "Evolve now" (`coop.creations next` is
refused). `coop.creations` prints `creature mode: host|own`.

**Scopes.** *Session*: the host sets it before the start, that is while its game plays no world (the galaxy menu,
the lobby of F1); once the host plays its world it is refused ("a session setting: ..."). *Live*: the host can change
it in play too. A guest never sets one (refused: "the host decides"). `coop.settings force` (tests) skips the scope.

**Where the values come from.** The host (or a player alone) has one table in force:
* a world opens (the game plays a stage with its own creature and the save name is known): if the host changed
  settings while no world was open (the lobby), those are the world's from now on; else the world's stored values;
  a world without a file takes the defaults of a new world (the built-in default, the `coop.ini` key over it);
* the table is written to `<profile>/Spore/SporeCoop/settings/<crc32 of the lower-case save name>.txt` (`world
  <name>` then `<key> <value>` lines) on every open and every change; a key this build does not know is skipped, a
  missing one takes the default, a file of another name (a crc collision) is not used. The save itself is never
  touched. Two saves of the same name share a file (the same known limit as the world id, host_world.h).
* a guest keeps no file of the host's world.

**On the wire.** `MSG_SETTINGS` (66, host -> guests, reliable, class Session in host_world.h msg_class): u8 kind 0,
u8 flags (1: the host plays its world), u8 n, n x {str8 key, u32 value}. The whole table goes to a guest on its join
(`settings::on_player_join`, before the world share) and to every guest on every change, on a world open and when
the started flag changes. By name: an older guest ignores the type, a newer one skips keys it does not know; a
setting the host did not send keeps the guest's own value, and a guest of an older host plays with its own values
(as before 0.7.12). A guest drops the host's table when the session ends. A live change shows a feed line on the
guest ("The host set Regeneration near a partner: off").

**Console.** `coop.settings` lists every setting (`key=value on|off [scope, default, changed] group: label - help`,
first line: host / guest / alone, the open world, started, pending); `get <key>`, `set <key> on|off|default|<n>`,
`force <key> <v>` (tests), `stored` (the stored file of the open world). `coop.dna pace 0|1` forces `all.dna_pace`.
The support report's `settings:` line lists what differs from the built-in defaults.

**Tests.** `coop_settings.py` (suite `settings`): the joining guest gets the table, a session setting is refused in
play, a guest cannot set one, Cell regeneration on heals both cells next to each other (the game's health), off (a
live change) the running guest gets it and neither cell heals, a session setting set in the galaxy menu is accepted,
and after both games end and start again the host's world plays with its stored values and the new guest gets them.
`SPORE_FIX_OFF=settings.cell_regen` fails the "off" checks. `twoinst.forget_settings()` removes the stored files at
every test start (cold and warm), and `coop.settings set|force` and `coop.dna pace` are lane knobs.
