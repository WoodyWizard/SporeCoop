# Changelog

## [Unreleased]

### Fixed
- Tribe: after the Creature stage ended the players' way, the host saw no guest in its Tribe world
  (no partners, "together" never reached) until its next save. The host now stays in its planet's
  world through the game's temporary save name (BUG-0258).
- Tribe: after the host visited an editor, the co-op epic creature was no longer tracked: it went
  into the shared save and the world of every guest, stayed after the session and a second one
  came later. An editor visit no longer counts as a new world (BUG-0217).
- Civilization: a single frame without the player's first city could make the host forget every
  vehicle and city owner (vehicles rebuilt on the guests, captured cities and bought vehicles given
  to someone else). Only a real world change does that now (BUG-0223).
- Civilization: with guests in the session only a capture's caption closes itself after 8 s; other
  cinematics of the host (city purchases, camera cinematics) play to the end again (BUG-0224).
- Saves no longer keep the empty herds made for a partner's creature after the partner left, and
  every save builds its own list of co-op objects to leave out (BUG-0255).
- Windows: a received world that could not replace its old copy (another program had a file of it
  open) now goes into another save slot instead of being dropped; leftovers of interrupted world
  writes are tidied, an old world moved aside is put back or kept as a backup (BUG-0254).
- **A cancelled evolution vote no longer opens every guest's editor**, and a guest who leaves (or
  loses the host) right after a vote no longer gets its editor opened offline. The Give recipient
  you picked is forgotten when that player leaves or the session ends (BUG-0234). Needs the new
  version on the guest.
- **Shared money or tribe food a guest spends right when another player joins is no longer given
  back** (it was spent twice: the purchase was made, the money returned, the host never counted it)
  (BUG-0220).
- **After an evolution, every waiting guest gets the world when the cinematic is over (at most
  120 s)**: with two or three guests the waits added up (120 / 240 / 360 s), and a leftover wait
  could send the world during the cinematic, so a guest got the hatchling creature again (BUG-0232).
- **Civilization: a guest who plays another world than the host's no longer gets the host's
  vehicles and cities applied to its own planet** (its own vehicles were taken over or removed and
  cities founded), and in Tribe and Civ its own tribe or vehicles play on under its own orders
  until it plays the host's world (BUG-0231, part). Cities already founded that way stay in the
  guest's save.
- **"Host is in an editor / loading: you play on alone" is only shown in Creature, Tribe and Civ**,
  where the guest really plays on alone; in Cell and Space it is gone. DNA a guest earned while the
  host was away goes to the pack only when the host plays that stage again, and DNA the host owed
  is no longer paid into a different world it loads later (BUG-0233). Correction to 0.7.3.0: its
  BUG-0205 line says a guest plays on with its own creatures while the host edits; that
  holds for Creature, but Cell is still open (BUG-0205, BUG-0151).
- **Mixed versions: an older SporeCoop no longer loses long chat lines or gets an emptied species
  table** (BUG-0219): players tell each other at the join what they support; a long chat line
  reaches a 0.7.2.x or 0.7.3.0 player cut, a big species table does not reach them, and a
  notification names the player with the older version. A guest with a 0.7.2 host no longer keeps
  every Cell meal in memory.
- **A player who is loading or saving is no longer dropped when somebody with the same name joins**
  (BUG-0235): a rejoin after a crash is recognised by a token the game keeps until a clean leave,
  not by the name; such a rejoin now also works in a full 4-player session.
- **Pasting the co-op window's whole "Friends connect to: ..." line into Join works** (BUG-0236).
- **In-game update: a failed attempt no longer keeps the downloaded files in memory** (BUG-0241),
  a pack with two names for one file is refused (BUG-0242), and a second start of the mod or the
  game's exit no longer interrupts an update between two file moves (BUG-0243).
- **Settings: a coop.ini saved as UTF-16 BE is no longer destroyed**, and two games on one folder no
  longer lose a name written at the same time (BUG-0245).
- **Crash reports name the thread that crashed** (BUG-0248).
- **Space: a disabled ship is no longer destroyed when you come back from an editor or an
  adventure**: the 20 s for a repair stand still while you are out of Space, and a load or the
  galaxy menu ends the disabled state without a death (BUG-0229). A disabled state that ends while
  you are out of Space (the session ends) no longer leaves your ship invincible (BUG-0228).
- **Space: spice of the co-op gift that comes back while you are out of Space is no longer lost**:
  it goes back into your cargo hold once you are in Space again. A gift received in an editor or an
  adventure is now sent back like one received in the galaxy menu (BUG-0226).
- **Space: a partner's ship whose invincibility the game clears stays the partner's ship** instead
  of turning into an armed NPC ship next to a new one; a ship a game mission takes is held still
  and removed once the mission lets it go (BUG-0218).
- **Space armada contracts: a wave resumed after the host's return gets tough ships, is not won
  without a fight, and a raid lost while the host was away fails the contract** (BUG-0227).
- **Space allies: a galaxy reload is seen even when the new empire object gets the old one's
  address** (BUG-0225, the allies part).
- **Cell: a knocked-out partner no longer flickers to "up" when one update is late** (BUG-0238):
  it stays translucent until its own 10 s run out, is not taken for a helper meanwhile, and its
  death effect does not play twice.
- **Cell: a guest's meal while the host is in its editor no longer drops the guest's food later**
  (BUG-0239): the guest is back at the shared total as soon as the host returns.
- **Cell: a guest's bite no longer hurts cells the host's own bite cannot** (BUG-0240): rocks,
  plants and meat are spared, as in the game.
- **Setup: a failed GitHub check can no longer overwrite the release an install just fetched** (the
  window said "GitHub not reachable" and the next Install asked again; BUG-0244).
- **Crash reports name the game instance and whether a test profile was used** (`run:` line, no
  names or paths), so the test lab no longer counts a crash of your own game against a test
  (BUG-0249).

## [0.7.3.0] - 2026-10-28

A big bug-fix release. A review of the whole mod and the test lab found about 180 problems; 158 of
them are fixed here, nearly all with an automated test that fails without its fix: crashes and freezes,
desyncs between the players in every stage, worlds and saves that could be overwritten or mixed,
the setup and the in-game update. 0.7.3.0 plays together with 0.7.x, but both players need 0.7.3.0 for
most of the fixes. Correction (BUG-0219): one wire feature is new, reliable messages bigger than
one packet are sent in pieces (BUG-0034), which 0.7.2.x cannot join: with a 0.7.2.x player a chat
line above about 1160 bytes is lost and a Creature species table of 42 or more species can arrive
empty. The version after 0.7.3.0 sends such messages only to players who can join them. What is still open is listed in
[docs/known-issues.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/known-issues.md).

**GOG 2016 build (`922af7c7`):** fixes that need new game addresses switch themselves off on it
(see the README); use the GOG 2024 or Steam build for them (BUG-0108). Correction (2026-10-28,
BUG-0216): two fixes of this release are off on GOG 2016 too: NPC nations no longer spawning
vehicles on a guest's game (BUG-0188) and keeping our creatures out of saves (BUG-0020); the list is
[data/symbols/frozen_off_922af7c7.txt](data/symbols/frozen_off_922af7c7.txt).

**Behaviour change (added 2026-10-28, BUG-0216):** a Tribe or Civilization host who stays 4 s in an
editor or on a loading screen now counts as away: guests play on alone ("... is in an editor: you
play on alone") until the host is back (BUG-0205).

### Fixed
- **Joining by host name no longer freezes the game while the name is looked up** (BUG-0204): the
  name is resolved in the background and the join goes on when the answer comes; an unknown name
  ends the join with "cannot resolve <name>". Numeric addresses never waited.
- **Setup: the newest update pack next to the setup is used, and an older version is never
  installed over a newer one without asking** (BUG-0094): the button says "Install older x.y.z"
  and asks first; silent mode refuses unless `/downgrade` is given.
- **Setup: the window cannot be closed while it installs or removes SporeCoop** (BUG-0139), which
  could leave the game without `steam_api.dll`; clicking Install before the GitHub check has
  answered can no longer corrupt the setup's memory (BUG-0077).
- **install.bat keeps your settings on a reinstall** (BUG-0080): `mods\coop.ini` (name, last address,
  `[update] check=0`) and `mods\sporekit.ini` are only copied when missing.
- **Docs: GOG 2016 players are told which fixes their build does not get**: the fixes since 0.7.2.2 that need new game addresses switch themselves off on the old GOG 2016 build; the README and these notes now say so (BUG-0108). `INSTALL.md` names the right log line to check an install (BUG-0165).
- **Creature: a goal card's reward a guest earns while the host is in its editor is no longer
  lost**; it is paid when the host's creature is back. A guest whose host stays in an editor or on a
  loading screen plays on with its own creatures, as when the host is in the main menu (BUG-0205).
  Correction (2026-10-28, BUG-0216): BUG-0205 is still open: the Creature part is not proven in a
  game test and the Cell part is not done.
- **A damaged creations package (My Creations) can no longer crash the game while it is read**:
  a 64-bit index offset could wrap the bounds check (BUG-0208, found by the new decoder fuzzer).
- **Space: the "Jump to" button of the co-op window jumps to the partner it names** (with 3 or more
  players a partner in your own system made every press fail), and the notice names that partner
  (BUG-0072). Flying to a partner in the galaxy view is refused while your ship is disabled
  (BUG-0063).
- **Space: galaxy map name tags react only to clicks on the game**: a click in another window over
  the game no longer starts a trip, and with swapped mouse buttons the primary button works, the
  secondary one (camera drags) no longer flies the ship (BUG-0090, BUG-0145).
- **Space: the guest no longer sees defender ships twice** in the host's system, and a game ship
  can no longer be taken for a partner's ship (BUG-0111).
- **Space: an armada contract wave goes on with its own raid when the host leaves the colony and
  comes back**, instead of starting a second raid whose end decided the wave (BUG-0046). A won raid
  on a partner's colony is no longer taken for an aborted one (BUG-0136).
- **Space: unvisited stars get the same planets on both games** even when another thread names
  things during the generation (BUG-0123); `coop.space empire contact` no longer breaks the game's
  float maths after 8 calls (BUG-0126).
- **Saves no longer keep the partners' creatures** (BUG-0020): a host save made while a partner
  played kept the partner's creature and the herd made for it; after a restart it came back as a
  strange creature of another species (the 0xD48634 crash came from attacking it). They are now left
  out of every save. Test: `coop_save_hygiene.py` (suite `save_hygiene`).
- **Cell: a guest's bites follow the game's rules** (BUG-0078): a guest could hurt creatures the
  host's own bite cannot (species that never attack the player, a creature just bitten or
  invulnerable), and such species turned on the guest. Test: `coop_cell_guest_bite.py`.
- **A downed partner no longer rescues you** (BUG-0131): in the Cell stage two knocked-out cells
  side by side revived each other, and in Space two disabled ships repaired each other (also a
  partner whose game froze or was loading counted). A lethal hit with nobody able to help is the
  normal death. Tests: `coop_cell_rescue.py`, `coop_space_helper.py`.
- **Cell: small display fixes** (BUG-0160): a cell no longer shows squashed for a moment when its
  rotation is not set yet, a dying creature no longer shows full health on the guest, and the
  partner arrows never get a broken position. Test: `make test`.
- **Joining over a lossy link no longer fails when the host's first answer is lost** (BUG-0030):
  the lost welcome counted as delivered and the join ended with "no answer from host (timeout)"
  after 10 s (about one join in ten at 10 % loss). Test: `make test`.
- **Co-op window: the address field and the status line** (BUG-0138, BUG-0104): an address pasted
  with spaces around it (or the whole "Friends connect to" line) joins the first address; a port
  that is not a number from 1 to 65535 is refused at once instead of a 10 s timeout. The status
  no longer says "Connecting to ..." for the whole session: it clears when you are in, shows why
  a session ended, and button messages go away after a few seconds, so the update line shows
  again. Test: `coop_ui_status.py` (suite `ui_status`).
- **Co-op window: more than three games on the network** (BUG-0183): "More >" pages through all
  of them; the list forgets hosts that stopped answering and holds at most 32 (BUG-0039).
- **No "player0 (host) is back: your N DNA go to the pack" after a disconnect** while the host was
  in the main menu (BUG-0085).
- **A partner no longer freezes when the other PC has been up for 49.7 days** (its millisecond
  clock wraps, BUG-0053). Test: `make test`.
- **Creature: a guest that gets the world right after the host evolved no longer stays a
  hatchling** (BUG-0019): the world went out while the host's evolution cinematic waited for OK,
  and the guest's creature never grew up nor migrated with the species. The host now sends it once
  the cinematic is over. Test: `coop_evolve_world.py` (suite `evolve_world`).
- **Creature: after a reload the guest plays its own creature again** (BUG-0119): the mod told the
  worlds apart by the avatar's memory address, which the game can reuse; the guest could keep the
  host's species, or get its creature applied while the world was still loading. Test:
  `coop_world_reload.py` (suite `world_reload`).
- **Creature: a guest that rejoins while the host is paused sees the animals around it**
  (BUG-0125) instead of an empty area until they walked away and back. Test:
  `coop_npc_rejoin_paused.py` (suite `npc_rejoin_paused`).
- **Creature: an animal that pushes against a rock on the host no longer stays a few metres off on
  every guest** (BUG-0169): its copy is put in place after 3 s. Unit test in `make test`.
- **A fault inside the game during a co-op adjustment no longer leaves the game in the adjusted
  state** (BUG-0110): the cell stage's player index, a selection flag, a creature's tuning and the
  Civilization capture attribution are put back on the next frame. Unit test in `make test`.
- **Creature: social ability keys are ignored while the cheat console is open**, and a digit typed
  in another window no longer plays one when the game comes back to the front (BUG-0149; correction
  2026-10-28: it has a game test now, `coop_social_console.py`, FAIL without the fix).
- **Crash reports in hard cases**: a stack overflow now writes a crash report (before, the game
  vanished without one), and a crash in a game that ran out of memory still writes the minidump and
  tells you at the next start where the report is (BUG-0056, BUG-0127). Test: `kit_crash.py`.
- **Old crash reports are removed**: `mods\logs` keeps the newest 5 crash reports with their
  dumps, 5 other reports and 3 report archives instead of collecting them for ever (BUG-0137).
- **Player names in any language**: a Cyrillic or Chinese name survives a restart (it was saved as
  "????"), and a name typed into `coop.ini` with a normal editor (UTF-8) is read correctly
  (BUG-0142); names set in the console are capped at 24 characters and cannot change during a
  session, long texts are never cut inside a character, the creations path in the log keeps its
  non-Latin letters (BUG-0164). Tests: `win_unit.py`, `make test`.
- **Chat and notifications**: a Russian or Chinese chat message gets the same room as an English
  one in the notification panel and never ends in a broken character; chat always looks like chat
  (a message with "lost" in it is no longer a red alert, a player named "Lost" joining neither),
  and the co-op window's chat wraps long lines and scrolls back through the last 50 lines with the
  arrows on its right (BUG-0129).
- **Name tags and banners with non-Latin text are centred** (Cyrillic names, the arrows of off-screen
  partners, the Russian contract banner) (BUG-0159).
- **The co-op window after a resolution change** opens centred on the new screen instead of partly
  off it, and the overlay's co-op status text is no longer drawn over the co-op window or the
  notification panels at 1280x720 / 1366x768 (BUG-0113, BUG-0155). Test: `coop_window_layout.py`.
- **Hot reload of the mod no longer risks crashing a game thread** that was running a re-hooked
  function at that moment (BUG-0202). Test: `kit_freeze.py` (hookrace).
- **Host in the main menu (Tribe, Civilization)**: guests now really play on alone: their tribe
  members and vehicles move and follow their orders until the host is back (before, everything
  stood still). DNA a guest earns while the host is in an editor, on a loading screen or in the
  menu now reaches the shared pool, also from goal cards (BUG-0151). Tests: `coop_tribe_away.py`,
  `coop_civ_away.py`, `coop_dna_away.py`.
- **Tribe epic creature**: an NPC tribe's villager hitting it no longer counts as a second player
  (30 food, not 80, for one player alone); the epic no longer leaves a new herd in the world each
  time and is not written into the host's saves; it ends with the session, and the next session's
  first epic comes after 8 minutes again (BUG-0146, BUG-0121, BUG-0067). Test: `coop_tribe_epic.py`.
- **Civilization: vehicles of a player who left** go to a player who is still there (the owner of
  the nearest city, else the host) instead of staying locked; a captured city never goes to an
  absent player (BUG-0031). Test: `coop_civ_leave.py`.
- **Civilization superweapon vote**: a second request during a running vote no longer changes the
  weapon and target of that vote; its sender is told to ask again later (BUG-0117). Test:
  `coop_civ_sw_vote.py`.
- **Civilization capture caption**: with guests in the session the host's caption closes by itself
  after 8 seconds, so a host who is away from the keyboard no longer stops everybody's game
  (BUG-0187). Test: `coop_civ_caption.py`.
- **Civilization city alarms** show symbols the overlay font has ("×" under attack, "†" being
  converted) instead of an empty box (BUG-0161).
- **Gifts with 3 or 4 players**: a ">" next to the co-op window's Give button picks which player
  receives the selected units (`coop.give next` in the console); before, a guest could only give to
  the host (BUG-0075).
- **Shared money and food**: money or food changed while out of a session (playing alone,
  another save) is no longer added to the next host's pool when joining; two players spending at
  the same moment can no longer push Civilization or Space money below 0, and a guest's money no
  longer jumps back up for a moment after a purchase (BUG-0049, BUG-0106). Tests: `coop_civ_shared.py`
  (suite `civ_shared`), unit tests in `make test`.
- **Civilization roles**: a player who joins under the id of a player who left no longer gets that
  player's role (and its bonuses) on the host, and guests no longer see the roles of other players
  swapped after someone rejoined (BUG-0064, BUG-0079).
- **Civilization: the other nations no longer buy vehicles on a guest's own game** that the next
  update removed again (BUG-0188). Test: `coop_civ_ai_spawn.py`.
- **Tribe: a member knocked out on the host lies down on the guest too** instead of standing with
  its last health (BUG-0173). Test: `coop_tribe_ko.py`.
- **Civilization: an NPC city founded while a guest joined or loaded** now appears on that guest
  a few seconds later instead of never (BUG-0120). Test: `coop_civ_city_join.py`.
- **Long chat lines and big tables no longer stop a player's network traffic** (BUG-0034): a message
  bigger than one network packet (a chat line of more than ~1100 characters, the species table of
  a rich planet) is now sent in pieces; before, it and everything reliable after it (damage, DNA,
  NPCs, chat) never arrived, and the connection flooded the other player with empty packets.
  Tests: `make test`, `coop_chat_long.py` (suite `chat_long`).
- **A player who joins under the id of a player who left is shown with their own look**
  (BUG-0076): the host and the others no longer show the newcomer with the previous player's
  creature, cell or ship, and do not hand that creation to later joiners. With 3 or more players a
  leaving player no longer leaves a frozen "ghost" behind on the others (in Space a ghost partner
  disabled instead of destroyed your ship and could "repair" it, BUG-0109). Tests: `make test`,
  `coop_creations_rejoin.py` (suite `creations_rejoin`).
- **Smoother partners, NPCs, ships and vehicles on unstable connections**: after the latency rose
  (a world sent to a third player, Wi-Fi), partners and NPCs moved in steps for up to a minute; now
  for about 3 s (BUG-0133). The host's ships and Civilization vehicles no longer jump up to 40 m
  ahead after a late packet (BUG-0122). Ship, vehicle and tribe member ids are never handed out
  twice in long sessions (BUG-0154). Tests: `make test`.
- **Cell: steadier sync on a lossy link**: one lost or late state packet made the guest delete and
  re-create up to 14 of the host's cells (they blinked, lost their facing, a dying creature died
  again); a guest's own meal over a level line could be taken back for a moment by an older total
  from the host (a level down and up again). The food and DNA totals reach a guest that was loading
  or just joined within 2 s. Tests: `coop_cell_net.py` (suite `cell_net`), `coop_cell_sync.py`
  (suite `cell_sync`), unit tests `tests/cell_tests.cpp` (BUG-0032, BUG-0051, BUG-0088, BUG-0128).
- **Cell: knockouts end cleanly**: a partner who left or whose "up again" was missed in an editor
  no longer stays translucent with a frozen "KO" label; a session ending or the stage changing
  during or right after a knockout no longer leaves the player's cell invulnerable, translucent or
  moved. After a small respawn, a level-up gives the cell its normal size instead of up to twice
  that (BUG-0061, BUG-0070, BUG-0041, BUG-0083).
- **A received world never overwrites your game**: a guest with `auto_load_world=0` playing its
  co-op slot had that slot moved away and rewritten when the host shared again (BUG-0035); a new host's
  world was written over an old co-op slot of another host and mixed with its planets (BUG-0087); a
  world whose files could not be written (disk full, a file in use) was still loaded (BUG-0105). A
  received world now goes into its own slot (same host and planet, or a free one), is written into a
  temporary folder, checked and moved into place as a whole, and is never loaded half-written.
  Test: `win_unit` (Windows file handling under Wine).
- **A guest in another world is left alone**: a player who joined from inside its own game (or loaded
  its own save while connected) got the host's DNA, its own creatures removed and the host's cells,
  tribe and relations applied, and the game then saved that (BUG-0130). Test: `coop_join_other_world.py`.
- **World sharing with several players**: one save is made for all guests waiting for the world, not
  one per guest in the same moment (BUG-0112); a share for a player who already left no longer saves
  the host's game or holds its editors (BUG-0166); a world load no longer starts inside an editor
  that opened just before (it waits until the editor is closed; it crashed the game, BUG-0157); after
  Leave, the old host's world is no longer loaded once the editor closes (BUG-0074). Test:
  `coop_world_queue.py`.
- **Updates keep your own files**: SporeCoop's update deleted every `*.old` file in the game folders at
  each start, also a player's `SporeApp.exe.old` backup (BUG-0114; its own backups are now
  `*.sporecoop-old`); a file that could not be replaced at the next start was never replaced later
  (BUG-0150); a pack listing a file twice could lose the original file on rollback (BUG-0089).
- **Creature: NPCs on the guest match the host's after the game reuses a creature**: when the game
  built a new NPC in the object of a freed one, guests kept showing the old one (another species,
  or its corpse dragged along the new creature's path); NPC ids no longer repeat in long sessions
  (BUG-0044, BUG-0023). Test: `coop_npc_reuse.py` (suite `npc_reuse`).
- **Creature: a partner's creature removed during a cinematic** no longer shows up for that partner
  as an extra AI-driven copy of itself (BUG-0066). Test: `coop_cinematic_puppet.py`.
- **Creature: an error in an NPC's AI next to a partner** can no longer leave the partner's creature
  as the host's player creature for good (BUG-0110). Test: `coop_npc_swap.py`.
- **After leaving a session, a guest's own creation is its own again**: the host's creation with the
  same id kept replacing it until the game was restarted (BUG-0055). Test: `coop_creation_leave.py`.
- **Joint-hunt bonus**: the hit table no longer grows for the whole session, and a new creature does
  not inherit the hits of a dead one (BUG-0099). Test: `tests/npc_tests.cpp` (`make test`).
- **Space: a lost network packet no longer blows up the host's ships on the guest**: with more
  than 18 NPC ships around, one lost ship-state packet made up to 18 of them explode (with loot)
  and reappear on the guest (BUG-0037). Test: `coop_space_fleet.py` (suite `space_fleet`).
- **Space: a guest keeps its own escort ships** (posse) while it is with the host (BUG-0084).
- **Space: tool charges, spice and offers that come back while you are not in Space** (an editor,
  the menu, a session end there) are given back once you are in Space again instead of being lost
  (BUG-0058). Test: `coop_space_trade_refund.py`.
- **Space allies: after a load the partner's mirror empire is made anew** instead of putting the
  old galaxy's empire back at its old id, where a real empire of the loaded galaxy could live
  (BUG-0038). Test: `coop_space_allies_reload.py`.
- **Space diplomacy no longer shares the allies mirror empire**: a guest could become allied with
  and contacted by an empire of its own galaxy it never met (BUG-0071). Test:
  `coop_space_diplo_mirror.py`.
- **Players with the same name are told apart**: the host adds " 2", " 3" to a name already in the
  session, so two "Player" guests no longer share one allies mirror, rivals relation or budget
  (BUG-0141). Test: `make test` (net_tests). A player who rejoins right after a crash keeps its
  name: its old silent entry is replaced (BUG-0209). Test: `coop_nest_species.py`.
- **Hardened the handling of network messages from other players** (BUG-0201). Tests: unit tests
  `tests/validate_tests.cpp` (`make test`), `coop_net_robust.py` (suite `net_robust`).
- **SporeKit's diagnostics never freeze the game**: `stack`, `hangdump` and the hang watchdog could
  stop the game for good when the game was allocating memory at that moment; `sk shot`,
  `sk wait.frames`, `sk stack`, `sk hangdump` in the cheat console froze it (they are now refused
  there); a fault inside the exception logger or the log froze the game without a crash report.
  Removing a mod's hooks (quit, hot reload) no longer frees code another thread may still run.
  Test: `kit_freeze.py` (suite `kit_freeze`, BUG-0050, BUG-0065, BUG-0115, BUG-0132).
- **Cell: leaving co-op** removes the guest's mirrored cells so they do not remain in its pool.
- **Leaving co-op cleans up every stage**: after a guest pressed Leave in Space the host's ships it
  showed stayed in its game and flew on by themselves; starting to host or join while still in a
  session kept the old players. Every way a session ends (Leave, the host gone, a new host or join,
  a mod reload) now cleans up the same way. Test: `coop_leave_cleanup.py` (suite `leave_cleanup`).
- **Space: possible crashes in fights and jumps**: a hit that arrived right after leaving a planet
  could hand a ship the game had already removed to the game's damage code (BUG-0054), and a star
  id from the network could make the mod read past the end of the star table (BUG-0092). The
  "ship disabled" state no longer writes to the ship after Space was left (BUG-0098). Test:
  `coop_space_stale.py` (suite `space_stale`).
- **Space: rescue timing**: from the second time a ship was disabled on, a partner repaired it in
  1.5 s instead of 2 s (BUG-0147). After a failed co-op jump, the player's own zoom-out to the
  galaxy no longer loses the game's galaxy caption and hints (BUG-0095).
- **Creature: fewer crashes around a world change**: a partner's animation (or leave) arriving while
  the guest's world was being reloaded went to the freed creature of the old world, and a guest kept
  pointers to its own creatures while the host was in the main menu and used them when the host
  came back. Tests: `coop_anim_teardown.py`, `coop_host_away_kill.py`.
- **Civilization: a load or a Leave no longer leaves old vehicles and cities behind in the mod**:
  after the host loaded another save (or left) during a city alarm, the defenders' speed boost could
  write into vehicles that no longer existed, and the host's and guest's vehicle and city tables
  kept the old world (new vehicles could inherit old owners). Test: `coop_civ_reload.py` (suite
  `civ_reload`).
- **Tribe: a chief that leaves the tribe** no longer gets its size put back through a removed
  citizen (possible crash), chiefs get their normal size back when the session ends, and no member
  is added to a tribe that is out while the game-over caption is up. Tests:
  `coop_tribe_chief_removed.py`, `coop_tribe_gameover.py`. Correction (2026-10-28, BUG-0215): the
  game-over part is a guard, not a fix: its bug was never reproduced (BUG-0172, its test passes
  without the guard too).
- **A host that left right after a guest joined could not leave its stage**: Leave within a few
  seconds of the save for the guest's world kept that save "in progress" for good, and the game's
  exit to the menu (and the load of another host's world) never happened (BUG-0140).
- **Votes from a stage or session that is gone**: an evolution or superweapon vote that was still
  open when the host left the stage or the session no longer runs later (an editor opening by
  itself after hosting again, a superweapon on the planet that was left) (BUG-0043).
- **Crashes**: a guest rejected by the host (other game build, full session) or whose host left
  used freed memory (BUG-0029); the game could end with an abort ("abort() has been called") when
  it closed itself without the normal shutdown (BUG-0107); a click right after leaving the galaxy
  menu could call into the freed "Co-op" item (BUG-0163).
- **Hot reload of coop.dll** (developers, the lab): the update check thread and tasks queued by the
  old DLL no longer run into the reloaded one (BUG-0060).

## [0.7.2.4] - 2026-10-25

**GOG 2016 build (`922af7c7`):** the fixes of 0.7.2.4 that need new game addresses are not in it (Alt+Shift+Enter, the hatchling growing up, the pack, the Cell bites, meat and spin): on GOG 2016 they switch themselves off, the rest works. Use the GOG 2024 or Steam build for them (BUG-0108).

More fixes from players' reports (Creature stage, Alt+Shift+Enter). 0.7.2.4 plays together with
0.7.x (the network protocol did not change); both players need 0.7.2.4 for the fixes on the guest's
side (the guest's respawn, deaths it sees, the host's species at the nests).

### Fixed
- **No more crash on Alt+Shift+Enter** (player crash report, 0x7777BE). It is Spore's own "next
  display adapter" key: on a PC with two or more display adapters (two monitors, or an integrated
  and a dedicated GPU) the game rebuilt its graphics device on the other one and crashed, also
  without SporeCoop loaded (a bug of the game). The key now toggles fullscreen like Alt+Enter
  (players hit it with Shift still held). Test: `coop_alt_enter.py` (suite `alt_enter`).
- **Creature: a guest grows up again after a death** (player report: "after my death I'm in
  constant state of growing up from egg ... can't move at all"). A respawn puts the creature back
  into the nest as a hatchling, and the game waits for a family member of the nest to greet it and
  make it grow up. A guest has no family of its own (its own NPCs are removed, the host's ones it
  shows run no AI), so the guest stayed a hatchling for good: half the health, the egg shell on its
  head on both screens, no control. The guest's hatchling now grows up a few seconds after the
  respawn (the host's only when its family has not come for 40 s). Test: `coop_creature_respawn.py`
  (suite `creature_respawn`).
- **Creature: a creature killed by anybody now dies on the guest's screen too** (player report:
  "enemies still seen as alive even when dead for quest"). The guest's copy of a host creature had
  no AI of its own, and in Spore the death itself (falling over, becoming a carcass) is done by the
  creature's AI: a killed creature stood on with an empty health bar, alive for the guest's game.
  Test: `coop_creature_kills.py` (suite `creature_kills`).
- **Creature: creatures notice the partner like the host's own creature** (player report:
  "aggressive enemies didn't target my friend"). The game's creature AI only ever looks at the
  player's creature; on the host a creature nearer to a partner now treats that partner as the
  player (notices, chases, attacks; its hits go to the partner). Host option
  `npcs_target_partners` (on by default).
- **Creature: the guest sees the host's species at the host's nests** (player report: at the same
  nest the host saw its "Woopsies", the guest other creatures and "The Datzu are your allies!").
  Spore populates a planet with the player's own creations, and creations are numbered per player:
  a guest who made creatures has other ones under the same numbers, and its game showed those (and
  their names and standing) instead of the host's. The host now sends its own creations that its
  world uses, and the guest shows the host's creatures with them, under numbers of their own, with
  the host's standings. Both players need this version. Test: `coop_nest_species.py` (suite
  `nest_species`).
- **Creature: the guest no longer crashes when it reaches the new nest after a migration** (player
  report: "My friend crashed after I completed migration to a new nest", guest crash at 0xC6AE60).
  The guest's own game claims the new nest too and plays its cinematic with the creatures of that
  nest; the mod was removing those creatures (on a guest the host owns the creatures) while the
  cinematic still placed them. Creatures the mod removes during a cinematic now go when it is over.
  Test: `coop_migration.py` (suite `migration`).

## [0.7.2.3] - 2026-10-25

**GOG 2016 build (`922af7c7`):** the fixes of 0.7.2.3 that need new game addresses are not in it (Alt+Shift+Enter, the hatchling growing up, the pack, the Cell bites, meat and spin): on GOG 2016 they switch themselves off, the rest works. Use the GOG 2024 or Steam build for them (BUG-0108).

Fixes from players' crash reports and screenshots (Cell and Creature stage). 0.7.2.3 plays
together with 0.7.x (the network protocol did not change); both players need 0.7.2.3 for the
species transfer (creatures of species only the host has).

### Fixed
- **Cell -> Creature: the guest's game no longer faults when the session ends.** A guest that had
  played the Cell stage together kept the host's cells it showed ("mirrors") after the stage was
  left; when the session then ended (the host left or its game crashed), it removed them from a
  cell pool the game had already torn down. The mirrors and the partners' cells are now forgotten
  once the Cell stage is left, and the cell console commands answer only in the Cell stage. Test:
  `coop_cell_leave.py` (suite `cell_leave`).
- **Creature: the guest no longer sees NPCs the host killed standing alive** (player report). After
  the host's editor or a load the host numbered its creatures anew, and the guest kept the old ones
  next to the new: a living copy of every creature, which a kill on the host did not touch. The
  guest now drops them. Test: `coop_creature_desync.py` (suite `creature_desync`).
- **Creature: the guest sees creatures of species it does not have** (player report: "he doesn't
  even see my companion"). The host's planet is populated from the host's own creations and
  Sporepedia downloads too; a guest without them could not build those creatures, also not an
  ally in the host's pack. The guest now gets them from the host. Both players need this version
  for it (an older host does not answer, an older guest does not ask).
- **Creature: no more crash of the host after an evolution, and no copies of the partner's
  creature left in the world** (player report, host crash at 0xB88BEA). After the editor the
  partner's old creature stayed on the planet as a wild creature next to the new one, and the
  herd made for the partner's creature could get its home deep inside the planet: when that
  creature looked for land around its home the game read past its map and crashed. The old
  creature is now removed when the players are back, a partner who comes back into the world
  appears where it is (not between its last position and the planet's centre), and the herd of
  a partner's creature stays under it. Test: `coop_editor_strays.py` (suite `editor_strays`).
- **Creature: no crash when you attack or kill the creatures a save brought back** (player report,
  host crash at 0xD48634, three times). A save made while the partner played kept the partner's
  creature; loaded again after a restart it was a wild creature ("these guys appeared after I
  loaded save"), and attacking it crashed the game. Such creatures are now removed when a world
  loads (also from saves you already have), the game no longer crashes on any creature that lacks
  that data, and the partner's creature no longer leaves a new herd in the world (and in every
  save) each time it appears. Test: `coop_creature_e84.py` (suite `creature_e84`).

## [0.7.2.2] - 2026-10-25

**GOG 2016 build (`922af7c7`):** the fixes of 0.7.2.2 that need new game addresses are not in it (Alt+Shift+Enter, the hatchling growing up, the pack, the Cell bites, meat and spin): on GOG 2016 they switch themselves off, the rest works. Use the GOG 2024 or Steam build for them (BUG-0108).

Fixes from a player report (two brothers playing the Cell and the Creature stage). 0.7.2.2 plays
together with 0.7.x (the network protocol did not change), but the fixes need both players on
0.7.2.2: host and guest share the work of kills, meat and editors.

### Fixed
- **Cell: the host's creatures no longer spin on the guest's screen, and the guest can kill them.**
  A creature now faces the same way for both players (it kept turning on the guest, between the
  host and the guest), so its mouth and spikes are where they are for the host. The guest's
  killing bite and bites with a mouth that eats chunks now reach the host (before, the guest could
  wound a creature but never finish it). Test: `coop_cell_spin.py` (suite `cell_spin`).
- **Cell: both players see the meat of a kill, and a chunk is eaten once.** When the guest killed a
  creature, its meat appeared only on the guest's screen for a moment and the host's creature just
  vanished; now the kill happens on the host with the game's own death and meat, which both see. A
  creature the host kills also dies on the guest's screen (before it stayed there as if alive). Food
  from a chunk both eat at the same moment counts once. Test: `coop_cell_meat.py` (suite `cell_meat`).
- **Joining sends the game the host plays now:** a guest that joined a host who had started a new game
  could be put into an older game of the host (the host had to delete it). Every game of a galaxy is
  in the same save folder, and a planet named with letters outside English (for example in Cyrillic)
  was not found, so another planet of that folder went out. Planet names in any letters now work on
  both sides, the host always names the planet it plays (saving again if its file is not written
  yet), and the guest never loads another planet instead.
- **Editors in the Cell and the Creature stage** (player report): a guest can call a mate in the Cell
  stage and evolve its own cell (its mate was removed by the pool sync at once); after the host's
  editor the guest no longer crashes and sees the host's new cell or creature right away (a new
  creation was sent once without its records: the guest's game faulted building the host's cell, or
  showed its own body for the host until the next send); the host's mating call starts one vote,
  not a second one that ran on in the editor; a rejoin applies the creations again (writing their
  files failed while the game had them open); after the own cell editor the new cell stays the own
  one; the guest's own creature editor after the vote opens once its world has loaded (opened right
  after the start-of-Creature world arrived, it came up without parts or buttons and the guest was
  stuck in it). Test: `coop_editor_desync.py` (suite `editor_desync`).
- **Crash reports:** the minidump of a crash came out empty on some systems and then the archive was
  not made; now the report, the archive and the mark for the next start come first, the minidump
  (of the crashed thread) last, and the archive is made again with it.

## [0.7.2.1] - 2026-10-24

Finding problems: crash reports ready to send, kept logs, the version in the game. 0.7.2.1 plays
together with 0.7.0-0.7.2 (the network protocol did not change).

### Finding problems
- **Crash reports, ready to send:** when the game crashes, freezes for 30 s or closes right after an
  error, SporeCoop packs `mods\logs\SporeCoop-report-<date-time>.zip` by itself (the crash report:
  what crashed, where, the call chain, the co-op session, other overlays and mods; a minidump; the
  logs; the settings), and the next start of the game tells the player its name.
- **Logs are kept:** the logs of the last 10 runs stay in `mods\logs\previous\` (restarting the game
  after a crash used to overwrite the crash's log).
- **"Report a problem"** in the co-op window (F9) packs everything into one file to send,
  `mods\logs\SporeCoop-report-<date-time>.zip` (the report, this run's and the last runs' logs, the
  newest crash reports with minidumps, the mod settings with the last joined address hidden), and
  opens its folder with the file selected.
- **The version in the game:** under the Co-op item of the main menu and at the bottom of the co-op
  window (with the game build: GOG 2024, Steam, ...).
- The log starts with the environment: Windows or Wine, memory and the 4GB flag, the graphics card,
  and other programs that hook the game (Steam / Discord / NVIDIA overlays, RivaTuner, ReShade, ModAPI).
- A fault inside SporeCoop's own code is told in the game once, with where the report is.
- SporeKit 0.3.4: `env`, `report`, `crash.test` console commands; mod API `kit_version`, `build_name`,
  `diag_report`, `diag_context`, `logs_dir`, event `SK_EV_FAULT` (docs/02-modkit.md 2.7).
- Tests: `diag_check.py` (suite `diag`), `coop_creature_death.py` (dying alone and skipping the
  respawn cutscene, a player report that did not reproduce) with the `coop.team die` test command.

## [0.7.2] - 2026-10-24

**No Co-op item on Windows with the Steam overlay** ([issue #4](https://github.com/WoodyWizard/SporeCoop/issues/4)).
The mod loaded, but after the menu came up SporeKit got no frames from the game: another overlay
(most likely the Steam overlay while Steam is online) took over the game's `Present` call, which
is where SporeKit's frame begins. Everything the mod does each frame stopped: no Co-op item in the
menu, F9 did nothing, no network. The log shows it as `WATCHDOG (no frames, ...)` while the game
still draws.

- SporeKit 0.3.3 has a second frame source: the game loop (`App::Canvas::Tick`). When `Present`
  has not reached SporeKit for 2 s while the game window has the focus and is not minimized, the
  frames come from the game loop, and back from `Present` when it returns. The log says
  `frames: Present has not reached SporeKit ...`. In that mode the mod's on-screen labels (names
  above players, notes drawn over the picture) are off; the menu item, the co-op window, F9 and
  the whole co-op work.
- Tested on Windows 11 with the Steam version (offline mode) and `[sporekit] frames=loop`, which
  ignores `Present` the way such an overlay does: the Co-op item appears, F9 opens the window, and a
  Steam guest in that mode played with a GOG 2024 host in the Creature stage (world transfer,
  puppets, NPC damage through the host, shared DNA), no faults. In the normal mode loads of saves
  (Creature, Space) and stage changes never switched to the game loop. Not tested with the Steam
  overlay online (the case of the issue): please report how it goes.
- First test of the Steam version on Windows 11 (0.7.1 and 0.7.2): every feature on, Steam and GOG
  players together.
- `sporekit.ini`: `background_mode` was under `[mods]` in the template (no effect when uncommented);
  new `frames=auto|loop`. Console: `frames.source [auto|loop]`; `info` names the frame source.
- `tools/tests/coop_frames_loop.py` (smoke): co-op with the guest's frames from the game loop.

## [0.7.1] - 2026-10-24

**The Steam version works.** Steam's `SporeApp.exe` (since its October 2024 update) is the GOG 2024
game wrapped in Steam's DRM: the game code is encrypted in the file and unpacked when the game
starts, so SporeCoop found no addresses for it and turned co-op off ("no symbol database for this
SporeApp.exe build", [issue #4](https://github.com/WoodyWizard/SporeCoop/issues/4)). Now SporeKit
recognises the exe by its data, waits for the unpacked code, checks that it is exactly the GOG 2024
code and then uses the GOG 2024 addresses. Steam and GOG players can play together.

- Tested on Linux with Steam (offline mode) and Proton: every feature on, a Steam guest joined a GOG
  2024 host in the Creature stage (world transfer, own creature, the host's NPCs), no faults. Not
  tested yet on Windows.
- Install as for GOG: into the Steam game's `SporebinEP1` (`SporeCoopSetup.exe` and `install.bat`
  now name the Steam build). 0.7.0 and 0.7.1 can play together (the network protocol did not change).
- SporeKit 0.3.2: `data_sha256` / `text_sha256` in symbol files, the late boot for packed exes,
  `[sporekit] defer_symbols=1` for tests; `tools/exe_info.py` prints both hashes.

## [0.7.0] - 2026-10-23

"A galaxy together": the Space stage becomes a game of two (to four) rulers of one galaxy who see
each other, help each other, trade and finish the game together. Everything is on the new
**Space** page of the co-op window (F9 in the Space stage, the "Space >" button).

**0.7 cannot play with 0.6.x** (the network protocol changed): every player needs 0.7.0.

**0.7.x is the last version for the old GOG 2016 build** (`SporeApp.exe` `922af7c7…`). From 0.8 only
the current GOG build (the 2024 update, `dc04aee5…`, the code of the March 2017 patch that Spore
ModAPI also targets) is supported: update the game through GOG.

### Space: new
- **Partners on the galaxy map**: a name tag in the partner's colour on their star (or on their
  ship while they fly in the galaxy view), an arrow at the screen edge when they are out of view;
  a click on the tag flies you there. The Space page lists where everybody is ("Bob: at Fibucius
  [Fly there]").
- **Your own territories**: the stars with a partner's own colonies get a ring in the partner's
  colour, and the Space page lists them one by one with "Fly there".
- **Partner colonies as an allied empire**: a partner's colonies founded after you joined exist in
  your galaxy too, as the cities of "Alice's empire", an ally you have already met. You can fly
  there, land at their city, and fight off a raid on that colony in your own game: winning it
  counts as defended in the owner's game. The mirrored empire never reaches your save.
- **Tool gifts**: give charges of consumable tools (bombs, terraforming charges, the colony tool,
  monoliths, colony boosters and turrets, fireworks ...) to a partner. Permanent tools (weapons,
  drives) stay with their owner.
- **Trade between players**: at a star with a partner's colony, sell spice to it for twice the
  market value (an export bonus paid by the galaxy into the shared budget, up to 30 units per
  colony every 10 minutes); the spice arrives in the partner's cargo.
- **Co-op contracts**: every 10 minutes the host's game offers a job made for two: a **pirate
  armada** that attacks one of the host's colonies only while two ships of the crew are there
  (two waves of tougher raiders), or a **joint survey** of 2-3 nearby stars that the whole crew
  has to visit together. Money and a tool for everybody in the crew.
- **Shared diplomacy**: the players are one people for the NPC empires. A first contact, a gift,
  a mission, a trade route, an attack, an alliance, a war or a peace by one player changes every
  player's galaxy the same way, with a notice. The host's relation table keeps all copies equal.
- **The galactic core together**: partners are told when someone reaches the Grox zone or the
  core, and can fly to a partner who is there. The core waits until everybody in Space has
  arrived (or "Enter now"); then everybody sees the ending and gets the Staff of Life. A partner
  who was not there gets the gift too, also when joining later.
- **Rivals mode** (a host option): every player becomes a separate empire with an own budget (the
  shared money is split; switching back adds them up). Players can propose alliances, declare war
  and make peace on the Space page; at war, ships really damage each other (with the game's own
  death and respawn), and an enemy never repairs your ship.

### Fixes
- A guest who joined (or a host who loaded) in the first ~16-26 s after the galaxy menu came up
  could be stuck without a HUD in every stage: the menu's intro cinematic was left playing by the
  world load and took every click. The mod now ends it.
- The jump to a partner (and every co-op jump) into a star system nobody had visited arrived
  without planets and without first contact. It now travels like a player through the galaxy view:
  planets load, empires meet you.
- Two games generated different planets for the same unvisited star (rarer now, see known issues).
- A jump to an id that is not a star of the galaxy crashed the game (GetStarRecord).
- The Space page and the jump buttons handle many rows (pages with "<" and ">").
- Rescue on a planet: a partner flying over your disabled ship at another zoom height could not
  repair it (the distance was measured in 3D, and a ship's height on a planet is its zoom). The
  repair distance is now measured along the ground.
- A click on the main menu's **Co-op** item while the galaxy menu's intro plays (about 16-30 s
  after the menu appears) did nothing: the mod now skips the intro (like Esc) and opens the window.
- LAN discovery asks 20 ports (it found hosts on ports 47810-47819 only).

### Testing
- New two-game tests for every feature (`coop_space_map`, `_allies`, `_trade`, `_contracts`,
  `_diplomacy`, `_finale`, `_rivalry`, `_jump`, `_guest_ui`, `_page`), all with real clicks on the
  Space page, on both supported builds (GOG 2016 lanes and the new GOG 2024 lanes 9 and 10 under
  Wine). All fixtures were made anew for the GOG 2024 install, plus `space_empire` (drive,
  colonies, empire contacts).
- `tools/re_lint.py` checks every symbol the code uses against every supported build.
- Release gate: the full suite (45 tests) on 6 lanes, three GOG 2024 and three GOG 2016: 43 of
  45, and the two others (`space_rivalry`, `space_puppet`: the GOG 2024 landing delay after a
  load, now retried by the tests) passed 4 of 4 on GOG 2024 lanes afterwards.

### Known issues
- On GOG 2024, zooming in to land on a planet can do nothing for up to ~80 s right after a world
  load; then it works.
- Two games can still generate different planets for a star nobody visited (rarely).
- While a game panel that waits for OK is open (for example the badge points after a raid), the
  co-op window gets no clicks: press Enter or OK first.
- Details and more: [docs/known-issues.md](docs/known-issues.md).


## [0.6.2] - 2026-10-23

Builds with the same code as a supported one, and a list of where SporeCoop has been tested.
0.6.0, 0.6.1 and 0.6.2 can play together (the network protocol did not change); all players need
the same game build.

### Where SporeCoop has been tested

| Game build | System | Tested |
|---|---|---|
| GOG 2016 (`SporeApp.exe` SHA-256 `922af7c7…`) | Linux, Wine / Bottles | Yes: developed on it; every stage, 2-4 players, the stability runs of 0.6.0 |
| GOG 2024, the current GOG download (`dc04aee5…`) | Windows 11 | Yes (0.6.1): every stage as host + guest, two game instances on one PC |
| GOG 2024 with a changed header (same code) | Windows 11 | Only with a modified copy of the GOG 2024 exe (0.6.2) |
| GOG 2016 | Windows | Not tested (0.6.0 and older could not work on Windows, fixed in 0.6.1) |
| GOG 2024 | Linux, Wine | Not tested |
| Steam, EA app / Origin, disc | any | Not tested: co-op stays off unless the exe's code matches a build above |
| Two different PCs over the internet or a VPN | Windows | Not tested with 0.6.1+ on Windows |

The same list, kept up to date: [INSTALL.md, section 2](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/INSTALL.md#where-sporecoop-has-been-tested).
Tried another build? An [issue](https://github.com/WoodyWizard/SporeCoop/issues) with
`mods\logs\sporekit.log` (it names the exe's `sha256` and `code_sha256`) helps.

### Game builds
- A `SporeApp.exe` that differs from a supported build only in the PE header, the resources or a
  signature now gets that build's addresses: symbol files carry a `code_sha256` (the code and
  data sections and their addresses, `tools/exe_info.py`), and SporeKit uses it when no file
  names the exact exe. Such a player counts as that build when joining. Tested with a modified
  copy of the GOG 2024 exe (loaded at another base, joined a GOG 2024 host); a copy with one
  changed code byte is still refused.
- SporeCoopSetup tells such a build apart ("same code as the GOG 2024 build").
- INSTALL.md lists which game builds and systems SporeCoop has been tested on.
- SporeKit 0.3.1: `SkHost::symbols_build`.

## [0.6.1] - 2026-10-23

Support for the current GOG build of Spore, and a fix that made the mod work on Windows at all.
Tested on Windows 11 with the GOG 2024 build: host and guest (two game instances on one PC) played
every stage: cell, creature, tribe, civilization and space (joining, world transfer, puppets,
NPC damage through the host, shared DNA / money / food, civ purchases, buildings and orders, tribe
orders, ship damage, saving), with no faults.

If you installed 0.6.0 on the current GOG build, the Co-op item was missing and
`mods\logs\sporekit.log` said "no symbol database for this SporeApp.exe build". Install 0.6.1 with
`SporeCoopSetup.exe` or `install.bat` (the in-game update cannot start there, because the mod was
off). 0.6.0 and 0.6.1 can play together (the network protocol did not change), but all players
need the same game build.

### The current GOG build and Windows
- The current GOG download of the SPORE Collection (the 2024 update, `SporeApp.exe` 24 895 536
  bytes, SHA-256 `dc04aee5…`) is supported: it has its own symbol files
  (`march2017_dc04aee5.sym`, `overrides_dc04aee5.sym`). Before, SporeKit found no symbols for it
  and the mod turned itself off. Players of one game still need the same build.
- Windows: every game address was off by the exe's relocation when Windows loaded the exe at
  another base (ASLR, e.g. 0x410000). SporeKit now reads the exe's preferred base from the file,
  not from the header in memory, which Windows rewrites. Wine does not move the exe.
- The last hard-coded game addresses in the mod (vehicle, citizen, tree and drawable vtables, the
  civ growth and city planner return addresses, the allocator) are symbols now.
- `tools/build_windows.sh` builds the DLLs on Windows (Git Bash + llvm-mingw, no make).

## [0.6.0] - 2026-10-23

Installer, updates from GitHub, and a full stability campaign: bots played every stage for hours,
4-player sessions with rejoins, long chaos runs and bad networks. About 40 bugs found and fixed;
the numbers are in [docs/stability-report.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/stability-report.md).

All players need 0.6.0 (the network protocol changed; 0.5.0 cannot join).

### Install and update
- `SporeCoopSetup.exe`: finds the game, tells whether the build is the tested one, installs,
  updates and uninstalls with one button; downloads the newest release from GitHub (or uses the
  update pack next to it, offline).
- Updates in the game: SporeCoop checks this repository's releases at the start; the Co-op window
  shows an Update button; files are checked against the release's SHA-256 sums and installed for
  the next start (`[update] check=0` in `mods\coop.ini` turns the check off).
- Releases also carry `SporeCoop-<version>.pack` (the update) and `SporeCoopSetup.exe`.

### Crashes and freezes fixed
- A guest loading the host's world could crash in the galaxy menu's planet view, or come up on a
  planet without its height map (ground 6 m low, animals falling through). Loads now always run
  outside the game's frame.
- A guest could hang in the loading screen for good when it left to the main menu during an
  evolution vote (a save written while the editor opened); saves and editors now wait for each
  other. The host's own save no longer breaks when it leaves the game while a guest joins.
- Tribe: a rare fault when the stage was set up right after a load.
- Space: a fault when the respawn caption ended after the ship had left the planet; the host could
  crash when the game removed a partner's ship.
- Civilization: a guest could crash and freeze on a vehicle the game had removed.
- Creature: a guest fault in the creature event handler.

### Sync fixes
- Busy Tribe and Civilization planets (24+ citizens, many vehicles) no longer lose their state on
  guests: snapshots were larger than one packet and dropped every time.
- Animals stand where the host has them on every guest (some proxies were never updated).
- Cell: the host no longer destroys cells nobody ate; mirrored cells no longer fly away; the
  partner's cell no longer blinks.
- Creature and Tribe: killed animals no longer fly into the sky.
- Civilization: NPC cities, captures, ruins, spice claims and new cities match on every machine;
  money from derricks is no longer counted twice.
- Creature: a guest's kill pays DNA; guests keep the animals while the host's game is paused;
  goal cards no longer call species extinct.

### Gameplay fixes
- The host no longer leaves the game by itself: the mod used to answer "Do you want to save your
  game?" for 30 s after each save. Only the "game saved" box is confirmed now.
- Cell: the host's creatures hunt every player, not only the host.
- Creature: guests can befriend animals and make alliances; when the host goes to the main menu,
  guests are told and play on; name labels hide under menus.
- Tribe: guests can gather from fruit trees; gifted members can no longer be ordered by the giver;
  a knocked-out chief is replaced.
- Space: a disabled guest ship can be repaired in real fights; partners' ships no longer fire on
  their own.

### Testing
- Bots that play each stage through real input (`tools/tests/play_*.py`), 4-player sessions
  (`coop_multi.py`), longer chaos runs, regression tests for every fix; tests run on an invisible
  display. See [docs/06-testing.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/06-testing.md).

### Known issues
See [docs/known-issues.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/known-issues.md). The ones players can meet: the
host's capture cinematic in Civilization pauses everybody until the host presses OK; in Cell a
guest far from the host swims in an empty pool; the guest's social game in Creature is simplified.

## [0.5.0] - 2026-10-23

The first public release: co-op in every stage of Spore, with a shared world, everybody's own
creations and mechanics that only work together.

Tested with the GOG "SPORE Collection" build (`SporebinEP1\SporeApp.exe`, SHA-256 `922af7c7…`).
Install: [docs/INSTALL.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/INSTALL.md) - extract the zip into `SporebinEP1`, run
`install.bat`. All players need this same version.

### Playing together
- **Co-op** item in Spore's main menu: host or join (LAN discovery, address, VPN), lobby with
  players, stage, ping, readiness and chat; **F9** in game. Notifications slide in as native panels.
- One world in every stage: the host simulates, guests mirror it and act through the host; the
  host's world is sent to guests and loaded automatically, including after evolving.
- Evolution by vote: everybody presses *I'm ready* before the editor or the next stage.
- Own creations: every player plays their own creature, cell and ship from *My Creations*.

### Stages
- **Cell: Symbiosis:** shared tide pool and food; a knocked-out cell is revived by a partner
  right next to it, otherwise it respawns small next to the partner.
- **Creature: Pack:** joint hunting +25 %, the pack saves a dying player, healing near a partner.
- **Tribe: Chiefs:** members split between players, a chief per player with an aura, pincer
  attacks +50 %, duet performances, healer food, epic creatures at the village, gifts of members.
- **Civilization: Governors:** own cities and vehicles, military / economic / religious roles,
  joint sieges, city alarms with beacons, superweapons by vote, trade and gifts between players.
- **Space: Squadron:** shared enemies, rescue of a disabled ship, jump to a partner's star, raid
  alerts, mission reward tools for partners, terraforming together, spice gifts.

### Fixed in this release
- Loading a world while the Space stage ran crashed the game (every guest rejoin in Space); loads
  now leave to the galaxy menu first.
- The host's "game saved" box could stay open and pause the host.
- The Co-op menu item could hang the game after several returns to the galaxy menu.
- Cell teleports and respawns landed metres away.
- World transfer is about 3× faster (128-packet acknowledgement window).

### Known issues
See [docs/known-issues.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/known-issues.md): a rejoin over a very lossy connection can take
long; the guest's *Done* button in its own editor does nothing (*Cancel* works).

## [0.2.0] - 2026-10-21 (not published)

Co-op presence in every game mode: other players visible in Cell, Creature, Tribe, Civilization,
Space and Adventures, shared DNA, food and money, host world transfer.
