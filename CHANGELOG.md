# Changelog

## [Unreleased]

### Fixed
- Tribe: a chief saved by a partner's member ("<A> saved <B>'s chief") now keeps 30 % of its health; before, it kept
  0.3 hit points and fell at the next blow (BUG-1052).
- Tribe: a host's tribe member that puts its tool down, or starves, now shows that on the guests' screens too; before,
  the guest's copy kept the old tool and its last hunger (BUG-1043).
- Creature, Tribe: an animal of the host's that is thrown aside while it walks now jumps there on the guests' screens
  at once instead of sliding back over up to 2 s (BUG-0997).
- Creature: after the host evolves its creature in place, the guest's world takes the host's new species as the
  player species also when the new species arrives a few frames late (BUG-1057).
- Creature: a guest's social game shows the game's social meter also when the first try to open it came too early
  (BUG-1058).
- Civilization: while the host is in the menu or the editor, the guest's own game keeps the host's vehicles it now
  drives on ground they can use (BUG-1056).
- Creature and all stages: a creation of another player that changes often no longer runs out of package names
  ("cannot write the package") after a few dozen versions or rejoins in one game session (BUG-0996).
- Tribe, Civilization: a guest's order on a tribe member or vehicle that had just been removed in the host's game (it
  fell, was disbanded) no longer goes to the removed object in the host's game (BUG-1053).
- Creature: a guest's social click right after a creature change no longer starts the social game with the wrong
  animal, and an animal of the guest's world is no longer lifted into the air where a removed copy of a host animal
  floated (BUG-0994, BUG-0995).
- Space, Rivals mode: switching back to Allies while a guest is rejoining (its game still loading the host's world)
  now merges that guest's own budget; before, its budget was lost and the host's save money was added instead. A
  player new to a Rivals session now starts its own empire with 0 money, not with a copy of the host's money
  (BUG-1029).
- Space: after a Rivals period, the first sync of the shared colonies no longer takes the guest's older spice
  amounts as collected and lowers the host's colonies to them (BUG-1039).
- Shared money and food: when the host and a guest spend at the same moment and the pool runs dry, the notices no
  longer say the host earned money or food it did not earn (BUG-1009).
- After an error inside the game's drawing that the game itself recovered from, the co-op no longer stops for good
  while the game draws on (the partner stood still, chat and console went silent); the on-screen tags and partner
  arrows no longer vanish for the rest of the game after an error in them (BUG-1024).
- A console command that hit an error the game recovered from now answers with an error instead of never answering
  (BUG-0966).
- The partner arrows come back after a display reset (Alt+Enter) when the game failed to set them up once (BUG-1026).
- Crash and hang reports are written more safely: the crashed thread's registers are copied first, reports written
  in the background no longer call into the game, a crash near the end of a stack is recorded, and a very long game
  folder path no longer overruns a buffer at start (BUG-1020, BUG-1022, BUG-1028, BUG-1033).
- Tribe: a guest that joins another host, or the same host after it restarted its game, now sees the host's tribe
  move again at once; before, its game dropped every update of the new host as "old" for up to an hour (BUG-1051).
- Tribe: the host's game no longer loads its save again a few seconds after a guest joins, when an earlier session
  ended right after the host entered the Tribe stage and the host had loaded another game since (BUG-1054).
- Creature, Tribe: a fight sound or effect of a host creature no longer keeps playing on the guest's copy after a long
  session with many fights (BUG-1049).
- Cell: the mod's per-session Cell state (a meal received in the last moment of a session, the move next to the host
  after joining, the own cell's last good position and its small size after a respawn) no longer carries over into
  the next session or Cell game (BUG-1002).
- Space, Rivals mode at war with the host: the host's escort ships and system Defenders and the guest's ship now
  really damage each other (before, their shots hit nothing on either side) (BUG-1032).
- Space: a host's NPC ship that fires again after a short pause no longer shows a burst of extra shots, or no shot,
  on the guests' screens (BUG-1046).
- Space finale: "Fly to" a partner at the galactic core from the galaxy view no longer costs health with no energy
  left, and a ship disabled after the order no longer flies (BUG-1045).
- Space: a partner's terraforming no longer makes another player's game fault when that player's game never
  loaded the planets of that star (BUG-1042).
- Space, 3-4 players: "Give 10 to <player>" now gives the spice to the player the button names, and ">" picks the
  recipient in Space too (BUG-1050).

## [0.7.13] - 2026-11-12

A bug-fix update: the players' reports from GitHub and Discord, problems found by a review of the code, and many
more found by the tests and by looking at their screenshots, in every stage. Among them: Civilization vehicles that
got faster and faster, a crash when a guest further on in the Cell stage joined a host, a guest stuck in the
conquest caption in the Tribe stage, partner tags drawn over cutscenes, and screen tearing with Spore ModAPI.
Both players should update: the versions can still play together, but several fixes need both sides.

### Fixed
- Tribe: when the host's tribe conquers a village, the guest's game no longer stands still in the "You have destroyed
  the village" caption until the guest clicks it: it closes itself after 8 s, as the host's does (BUG-0983).
- Partner name tags and edge arrows are no longer drawn over a cutscene that holds the game, such as the
  Creature stage's intro (BUG-0985).
- Tribe: a guest now sees the host's members hold their staff or torch in the hand also when the copy is still in an
  earlier gesture (a short hold in the change of items was missed) or got the same gesture again while it played it
  (a held staff of several seconds was missed) (BUG-0992).
- Cell: a guest whose own game was at a higher level (for example food 300, level 4) no longer crashes the
  moment it joins a host at a lower level; the game drew the tiles of the new level at the old level's scale and
  overran its own buffer (BUG-0975).
- Civilization: a guest's vehicles sent to attack a wild creature no longer ignore the order when the host's
  game has that creature elsewhere (wild creatures are not shared): they attack the host's creature near there or
  drive there (BUG-0970).
- Tribe: a guest now sees a tribe member hold its torch, staff or tool in the hand whenever the host sees it: the
  copy missed holds when it was in another animation at that moment (a chief raising its torch on one game only), and
  when the member took a new item, the copy played the change with an empty hand (BUG-0968, BUG-0971).
- Tests: the unit tests' LAN discovery checks no longer fail when another test lane runs at the same time (BUG-0969).
- Galaxy menu: the Spore logo, the "Online Interactions Not Rated by the ESRB" line and the copyright line no longer
  stay over the game after joining from the menu right after it came up (BUG-0938).
- Tribe: while the host's tribe is out and the host's game shows "Alas! Your population has fallen below the
  minimum", a guest's game waits with a note until the host answers, instead of playing on against a stopped
  host (BUG-0960).
- Tribe: a member that dies leaves the roster on the right of a guest's screen too; the guest kept its card for the
  rest of the play (BUG-0961).
- Tribe: a guest's copy of a member holds its staff or instrument in the hand only when the member does on the host;
  in some gestures the copy held it for a second and a half on its own (BUG-0962).
- Network: player names, LAN game names and planet names received from other players are cleaned to one line of
  printable text (control characters and broken text removed, names cut to 24 characters) before they are shown,
  logged or written to the co-op save slot (BUG-0774).
- Network: a host answers "Games on your network" queries only from local networks and virtual LANs, and only a few
  times a second (BUG-0776).
- Network: stray packets from unknown senders no longer take the host's connection slots, so a player can always
  join or rejoin (BUG-0778).
- World transfer: a received file named like a Windows device (such as con, nul or com1) is refused instead of
  being written to that device (BUG-0785).
- SporeKit: a hot reload of a mod destroys its queued tasks outside the task lock, so a task that queues another
  one on its way out cannot freeze the game (BUG-0860).
- Network: starting a new session (host or join) from inside the handling of a message no longer has the new
  session closed right after (BUG-0861).
- Creature, Tribe and Civilization: the guest's whole-planet comparison no longer loses a list when a late piece of
  an older list arrives (BUG-0886).
- A partner's name tag and arrow stay on the partner while you turn the camera; during a fast turn they used to
  jump beside it or over empty ground (BUG-0948).
- Creature: a partner's name tag stands just over its creature's head; over a crouching creature it floated a metre
  and more above it (BUG-0929).
- Partner arrows and name tags no longer cover each other, the minimap, the bottom bar or message cards: they move
  aside, and a tag whose creature is behind the HUD is hidden (BUG-0949).
- Creature: the portrait at the bottom right of a guest's screen shows the creature the guest plays now, also after it
  changed its creature or became the host's new one; it kept the first creature (BUG-0937).
- Tribe: the music of the dance at the fire pit stops for a guest when the dance ends, also when the guest was in
  an editor at that moment; it used to play on until the guest loaded another world (BUG-0890).
- Tribe: a baby tribe member grows up on a guest's screen when it grows up in the host's game, also when the guest
  joined or came back while the baby was growing; it stayed a baby there for a while longer (BUG-0857).
- Tribe: a tribe member the host buys at the hut is the host's. Before, every member the host bought went to a guest,
  and when a guest bought one in the same moment both could end up with the guest (BUG-0824).
- Civilization: a building a guest pays for in its city planner that the host's game cannot build (for example the last
  plot was just taken by the host's own building) gives its price back to the shared money, and the guest is told why;
  before, the money was lost and the building disappeared (BUG-0807).
- Tribe: a guest's tribe members no longer look starved in the roster after a few minutes of play (red cards with an
  empty bar); they are as hungry as in the host's game (BUG-0935).
- Tribe: a tribe member thrown into the air by a hard blow is in the air on the guests' screens too; it stayed on the
  ground there (BUG-0946).
- Tribe: tribe members at rest stand, sit or crouch the same way on every player's screen; a guest's copy no longer
  goes on with a gesture the host's member has already stopped (BUG-0936).
- Tribe: when a host's tribe member swings its staff or instrument, the guest's copy swings it too instead of holding
  it upright (BUG-0914).
- Tribe: a guest that joins again during Tribe has the same ground as everybody else round the villages that appeared
  during play; it was up to 8 m higher or lower there (BUG-0947).
- Cell: a player knocked out while a level change rescales the pool stays where it lay, next to the partner who can
  revive it; it was held far out in the pool (BUG-0798).
- Cell: a knocked-out cell no longer makes poison clouds that hurt and kill creatures for its owner while it is down
  (BUG-0799).
- Cell: a partner's poison clouds no longer hurt or knock out another player's cell while friendly fire is off
  (BUG-0958).
- Cell: the host's support log no longer says a knocked-out guest's refused kill was applied (BUG-0959).
- Cell: a guest's bite that the host refused or that did not kill no longer keeps that creature from being killed by
  any guest for the next 10 seconds (BUG-0821).
- Cell: a creature a guest swallows whole is gone in the host's game too; it lived on there and could be eaten again
  (BUG-0827).
- Cell: with three or four players the guests come back after a load or a knockout on spots of their own around the
  host, not on top of each other (BUG-0792).
- Cell: renaming a cell whose model came from elsewhere (a partner's or an imported cell) no longer crashes the host;
  the new name goes out with the model, and a cell without its model is not built (BUG-0802).
- Space: a guest that had earned a badge no longer crashes while it loads the host's world when it joins again from
  the same running game. A window the game had already destroyed was left in its list of open dialogs and was used
  when the next dialog closed (BUG-0956).
- SporeKit: a crash in which the game's stack pointer had left its stack (Windows and Wine end such a game at once)
  now writes a crash report as well; before, the game vanished without one (BUG-0956).
- Civilization: when the host saves and loads its world again, every player keeps the cities it had, captured and
  bought ones too; before, all cities were dealt out anew and a city the host had bought could become the guest's
  (BUG-0950).
- Cell: a guest that plays a Cell world of its own while connected (or is still in Cell while the host is already in
  the Creature stage) gets a living pool again: new cells and food appear, kills drop loot, predators hunt. Before, its
  pool only emptied (BUG-0797).
- Cell: the host no longer sends its live cells to a guest that is not in the host's Cell world yet (the menu, the
  loading screen, the world download, a world of its own); on a slow link they slowed the world transfer (BUG-0809).
- Creature: the computer creatures' nests evolve again in a world of a guest's own while it is connected (BUG-0803).
- Creature: in a world of its own a connected guest's game shows its own creations again, not the host's creature of
  the same id at its nests (BUG-0819).
- Creature: a guest that leaves the game in the middle of a social game no longer keeps the social meter on its
  screen (BUG-0808).
- Creature: when a guest leaves in the middle of a social game, the host's creature stops that game; it went on with
  its prompts and then played the failure reaction (BUG-0831).
- Creature: DNA a guest earns while the host is away (in the menu or loading) moves the guest's evolution bar once, as
  on the host; on a slow connection it could move it twice (BUG-0829).
- Creature: a hit by a UFO, a vehicle, a city turret or a tribe hut on a player's creature is credited to the right
  attacker; the mod read a wrong object as the attacker, for the pack's defence among others (BUG-0790).
- Creature: a guest's game no longer keeps the copies of the host's nest eggs in memory after a world load or a
  change of its creature (BUG-0833).
- Space: a guest that loads one of its own Space saves while connected keeps that save's cargo hold (tools, spice,
  cargo); it was replaced by the guest's co-op hold and saved that way (BUG-0816).
- Space: a guest's own Space save keeps its own stage history (the History bar and the archetype's cards); the host's
  was written into it (BUG-0818).
- Votes no longer wait their full 60 seconds for a guest that plays another world and cannot vote (BUG-0801).
- Civilization: vehicles no longer get faster and faster during sieges ("like flash", GitHub #199). Each new "city
  under attack" alarm took the already boosted speed as the vehicle's normal speed, so the +50 % boost piled up and
  stayed for good (BUG-0771).
- Civilization: a boosted vehicle gets its normal speed back when the mod stops tracking it while it still exists,
  instead of keeping the +50 % for good (BUG-0773).
- Civilization: the "city under attack" speed boost goes only to the players' vehicles, no longer to computer
  nations' vehicles, the attacking army included (BUG-0876).
- Civilization: a guest sees a fast host vehicle right where it is, not several metres behind it (a vehicle over twice
  as fast as the guest's copy of it, for example a boosted one) (BUG-0787).
- Civilization: a city a player captures stays that player's. With fewer cities than players the second city dealt
  every city out again, and the host's capture became the guest's a moment later; a capture that waits for its
  cinematic also lost its captor (BUG-0875).
- Civilization: a captured city goes to the player whose vehicles took it, not to the player whose vehicle stood
  nearest the city (BUG-0775).
- Civilization: a guest that buys another nation's city gets it in both games; before, the money was spent and the
  city changed hands nowhere (BUG-0877).
- Civilization: a city a guest captures while the host is away (a menu, an editor, a load) is captured in the host's
  game too when the host is back, instead of showing "won" on the guest's screen only (BUG-0878).
- Tribe: a village the host's tribe conquers is conquered in the guest's game too (the hut falls, the goal is done);
  it stood whole there (BUG-0777).
- Tribe: a village a guest conquers while the host is away stays conquered when the host is back; the host's game
  stood it up again (BUG-0879).
- Creature and Tribe: a guest's game could crash, or move one of its own animals as if it were the host's, when its
  game had just removed a copy of a host animal and built a new animal in the same place in memory (the mod now
  checks every kept animal against the game's object list and its serial number; it also covers Tribe now)
  (BUG-0842).
- Tribe: a guest could move a member of another tribe as the host's member, or remove it, when the game reused the
  memory of a removed copy (for example after the host was in an editor); the crash the PandizeLive fork reported
  here (BUG-0844).
- Tribe: a guest's game no longer faults when the game frees a copy of a host's member while the host's states pause
  (for example during the host's editor): the sizes, animations, held items and effects sent for that member no longer
  reach the freed copy (BUG-0955).
- Tribe and Civilization: the attack ring, the epic animal, the babies, the host's member and vehicle tables and a
  guest's vehicle copies now check that a kept object is still the same one, not another object the game built in its
  place (BUG-0951).
- Creature: a host's hit, an animal's social prompt or animation could reach a copy of an animal the guest's game
  had just removed; the removed animal was left in other animals' memory and could crash the game seconds later
  (BUG-0805).
- Creature: a partner's effect could be played on its creature in the frame the game removed it (BUG-0795), and a
  partner's animation on its cell or its Tribe marker around a stage change (BUG-0796).
- Creature: an animal of the world could disappear when a cinematic ended, if the game had put it where a partner's
  old creature waited to be removed (BUG-0825).
- Space: after the host loaded its world again, a partner's colony shown as an allied empire could bring the old
  world's cities into the planet of the new one (BUG-0838).
- Creature: an error caught inside the game's world-relevance pass or the own creature's size update could leave
  the game's list of view points on the mod's memory or the player's species changed for the rest of the session,
  and switch some effect and animation relays off (BUG-0882).
- Saves: when the mod saved while one of the game's questions was open (the pause menu's "Do you want to save your
  game?", the captured city's specialty question in Civilization), it answered that question with Enter and then left
  the real "game saved" box open with the game paused. The mod now confirms only the "game saved" box and leaves
  questions to the player (BUG-0846).
- Saves: after the mod confirms the "game saved" box the game follows your real mouse again at once; it kept reading
  the box's OK button as the mouse position for about 5 seconds (BUG-0804).
- Saves: the "game saved" box is also confirmed when its OK button lies outside the game window (after the
  resolution was lowered); it stayed open and the host's game stood still (BUG-0806).
- Sharing the world: a save file the host's PC cannot read at that moment (a virus scanner holds it) no longer
  makes the guest load an incomplete world; the share fails and is tried again (BUG-0847).
- Sharing the world on Windows: storing the received world no longer fails with "error 5" when a scanner looks at
  the new files for a moment; the move is tried again for a second and a half (BUG-0848).
- Tribe to Civilization: the host's game no longer saves for a joining player between the City Hall and the vehicle
  editors, while the game takes the village down; the world is sent in the Civilization stage (BUG-0849).
- Galactic Adventures: the mod no longer saves the campaign or posts its quit to the main menu while an adventure is
  played (BUG-0854).
- Space: a guest whose game crashed and who joins again at once gets its own colonies back, as after a normal rejoin
  (BUG-0810).
- Space, rivals mode: a guest's own money no longer jumps back after a visit to an editor or an adventure (a contract
  reward paid on the return was lost) (BUG-0813).
- Space: the galactic core no longer waits for a partner who already went in and flew away (BUG-0820).
- Space: spice a guest collects from a shared colony while the host is in an editor or an adventure can no longer be
  collected a second time (BUG-0822).
- Space: cargo that could not be put back into a guest's hold after a rejoin is no longer lost for good; it is tried
  again at the next rejoin (BUG-0826).
- Space: spice from a gift that comes back after the host loaded another game returns to the hold right away instead
  of being kept for the other game (BUG-0828).
- Space: with three or four players, a survey contract is completed when enough ships reach the stars, even if a
  player who joined the crew later stays at home (BUG-0830).
- Space: a spice sale that arrived is paid even when the seller was in an editor or an adventure as the session ended
  (BUG-0832).
- Space: the stored spice of every shared colony is kept in step, not only of the first 48 (BUG-0834).
- Space: a guest's own colony in a shared star system no longer loses its stored spice (BUG-0840).
- Space: the effects of a player's ship (world effects) now show on the partners' screens (BUG-0895).
- Space: when the host loads another Space save, the connected guests follow it into that game instead of playing on
  in the old one (BUG-0941).
- Space: a guest that leaves and joins again keeps its own badges and captain rank (and the counters the badges
  need); before, it got the host's badges back with the host's world (BUG-0786).
- Screen tearing: SporeKit no longer undoes a frame cap that another mod or program sets for Spore (it wrote the
  game's own cap back every few frames). The game now tells you once when it runs without its frame cap, and
  `vsync=1` under `[sporekit]` in `mods\sporekit.ini` turns on vsync: Spore itself asks for none. See
  docs/INSTALL.md, "The picture tears" (BUG-0779).
- RivaTuner / MSI Afterburner: the game tells you when RivaTuner is hooked into Spore and how to keep it out if Spore
  crashes at start; crash reports name it with the same advice, and docs/INSTALL.md has a section on it (BUG-0782).
- Creature: an NPC egg that hatches or is eaten in the host's game is shown so on the guests at once, not up to a
  second later. A guest's click at an egg that had already hatched on the host hit the hatchling inside it: a small
  body lay where the egg was and the host counted it as the guest's hunt (BUG-0896).
- Uninstall: the setup's Uninstall and uninstall.bat put the game's own steam_api.dll back also when an antivirus
  removed SporeCoop's loader, stop without changing anything while an update runs or when the game's original file
  is missing, say which file they could not remove instead of "removed", delete only SporeCoop's own symbol files,
  and uninstall.bat no longer puts an old backup over a steam_api.dll that Steam or GOG restored (BUG-0851).
- Tests: the setup test that closes the window after an install no longer fails now and then (BUG-0853).
- Creature: a creature with only a base paint (no coat and no detail layer) is checked for paints the other game does
  not have, like any other creature: a Creepy & Cute base paint now shows as a similar base game paint on a game
  without that pack instead of another colour (BUG-0852).
- The mod's hang diagnostics (the stack dump made when the game stops drawing) no longer risk turning a short stall
  into a freeze for good under Wine on Linux (BUG-0880).

### Changed
- Creature: while friendly fire is off (and outside Rivals) a player walks through a partner's creature and a click
  in the combat stance goes to the animal behind it; the partner's creature no longer stops the second hunter short of
  the prey. With friendly fire on nothing changes; players' creatures can now stand inside each other (BUG-0913).
- Antivirus warnings: every SporeCoop file now carries version information (product, version, description), each
  release file and the DLLs inside the zip get a signed build provenance from GitHub Actions (`gh attestation
  verify <file> --repo WoodyWizard/SporeCoop`), docs/INSTALL.md explains the false positives and how to check a
  release, and SECURITY.md lists what each file does (BUG-0784).
- tools/laa.py (optional, for big worlds): shows, sets and restores the Large Address Aware flag of SporeApp.exe with
  a verified backup; see docs/INSTALL.md (BUG-0862).

## [0.7.12] - 2026-11-11

A feature update built from the players' suggestions: a new co-op window, session settings chosen by the host,
arrows and minimap marks to find each other, player colours, and one report for the whole session.

### Added
- A new co-op window in steps. The first time it asks for your name (the one the other players see; *Change name*
  later), then *Host a game* or *Join a game*. Host: the port and the Host button, with the addresses to give your
  friends. Join: the games found on your network, and an address field that also takes a host name
  (`sporecoop.local`; port 47810 when you type none). The lobby shows each player's colour, stage, ping and ready
  state and the host's address, and has a **Settings** page (see below) (BUG-0765).
- "Report a problem" in the game's pause menu (Esc), below "Quit Spore": it makes the same report as the co-op
  window's button (BUG-0765).
- One report for the session: the host's "Report a problem" also asks every guest for its report, and the host's
  button "Ask the players for their reports" (lobby and F9) does it any time without the host's own report. Each
  guest's co-op window opens with "The host asks for your report: Send / Don't send" and says what is sent; nothing is
  sent without that click. The reports come to the host over the session and the host gets one file to send,
  `SporeCoop-session-report-<date-time>.zip`, with `Host-<name>.zip`, `Guest-<name>.zip` and a list of who sent, who
  did not, who did not answer and whose report was refused. A report over 25 MB or one that is not a SporeCoop report
  is refused (never unpacked or opened on the host's PC); no player's address goes into the file (BUG-0767).
- Session settings: the host decides which co-op mechanics are on, on the Settings page of the co-op window (or
  `coop.settings` in the console). The settings reach every player when they join and whenever the host changes one,
  and they are kept with the host's world, so the next session in that world plays with the same choices. Most can
  only be changed before the game starts; the "live" ones also during play (F9). Guests see them but cannot change
  them. The defaults keep today's behaviour (BUG-0762, BUG-0768). The settings:
  - all stages: partner arrows, partners on the minimap and name tags over players (live), progress at the
    single-player pace, NPC animals treating partners like the host's creature, how many players a session takes
    (2 to 4) and whether new players may join after the start (live; a player who lost the connection may always
    come back);
  - Cell: regeneration near a partner ("shared healing", live), the knock-out instead of death next to a partner,
    the respawn next to the partner as a small cell, the vote before the Creature stage;
  - Creature: pack rescue, partners healing each other (live), the joint hunt bonus, the host's evolution editor
    opening at once;
  - Tribe: the pincer bonus, the chief aura, the duet, the epic creature event;
  - Civilization: roles, the joint siege bonus, the superweapon vote;
  - Space: "disabled, not destroyed", trade, co-op contracts, shared diplomacy, partners as allies, the finale
    together, the rivals mode.
- Friendly fire (a setting, off by default): players' creatures can hurt each other in the Creature stage. Cell,
  Tribe and Civilization follow later; Space keeps its rivals mode (BUG-0768).
- One creature for everyone (a setting for Cell and Creature, off by default): every guest plays the host's species
  and only the host evolves it (its editor opens at once, no vote); the guests get no editor and no creation arrows,
  and every evolution of the host changes every player's creature at once (BUG-0769).
- Find each other: every partner who is off screen gets an arrow at the edge of the screen, in the partner's colour,
  with the name and the distance (Cell and Creature: the partner's creature, Tribe: its chief, Civilization: where
  the partner plays, else its capital, Space: its ship). On screen the name tag stays. Never in the Rivals mode
  (BUG-0764).
- Partners on the minimap: in Creature and Tribe every partner shows on the game's minimap as a mark in the
  partner's colour (BUG-0764).
- Player colours: every player gets one of four distinct colours (yellow, blue, pink, green), the same in every
  game; a player can pick another free one (eight in all) with the ">" next to the own colour in the lobby (or
  `coop.find color <name>`). Name tags, arrows, minimap marks, the galaxy map's tags and the chat lines show it
  (BUG-0764).

### Changed
- The co-op window's "Stop hosting" is now "Close session" (BUG-0765).
- The `coop.ini` lines `dna_pace`, `npcs_target_partners` and `teamplay` are now only the defaults for a new world;
  a world played before keeps the host's settings for it (BUG-0762).

### Fixed
- **Tribe: seconds of stutter and a "SporeCoop caught a problem" report on a guest after the host's game over.**
  When the host's game loaded its Tribe world again (a game over, or a load) and a guest's goal card was on a
  village that the host's world no longer had, the guest's game ran into the same error every frame once the
  village was removed there: a hitch of a few seconds and a problem report, until the goal moved to another
  village. The goal card now goes with the village (BUG-0942; since 0.7.7).
- **Space: shared money stopped for a guest after the host changed its save.** A guest that had joined before the
  host loaded another save (or went on into a save with another name) got none of the host's money in Space for
  the rest of the session, and its own earnings and purchases never reached the host; only leaving and joining
  again helped. The guest now follows the host's save (BUG-0933; since 0.7.11).
- **Creature: health after a change of the creature.** Switching to another creature (your own from F9, or
  the host's in a session) kept the old health number against the new maximum (16 of 10, or 10 of 16). The
  share of health is kept now (BUG-0928).
- Tribe: the guests' tribe roster (the member portraits at the right edge) showed white squares instead of the
  portraits when the tribe was of the host's own creature, which is the usual case after Cell or Creature. The
  host's creature now reaches the other players with its picture (BUG-0921; the fix of 0.7.10 covered only a creature
  the host was not playing).
- The host saw "<player> appeared in the world" twice when a player joined, and once more each time that player
  changed its creature. It is shown once (BUG-0923).
- Creature: a guest's game no longer stutters with "SporeCoop caught an error in its own code" when its baby is born
  after the evolution with animals nearby (the mod moved animals its own game had just removed) (BUG-0931).
- Creature: the host's game could end with a runtime error (a crash on Windows) a moment after a partner's creature
  died in a fight with a pack, mostly when the prey had just been killed: the pack's animals still wanted to help
  against the partner's creature that was no longer there (BUG-0926).
- Cell: while the host's game stands still (it reads a caption, a cinematic plays, the pause menu is open) a guest
  that swims on now gets the cells of the host's world around where it is. It kept getting the cells around the
  place it was at when the host stopped, and saw open water (BUG-0927).
- A game that stops with a runtime error message box (for example "pure virtual function call") now writes the
  message's text and where it came from into its log and makes a report, as it does for a crash (BUG-0925).
- Creature: a crash risk when you attack after your target is gone. If your creature's target was a partner's
  creature (you had clicked it) or an animal that then left your game (the partner died or left, the host's game
  removed the animal), the game kept aiming at it, and the next attack key read memory that was already freed. Your
  target is now cleared when its creature goes (BUG-0918).
- Space: terraforming now reaches your partners. The atmosphere, temperature and water tools (Atmosphere Generator,
  Ice Comet and the like) change the planet in the partners' games too (their game received the change but never
  applied it), also when no partner is on that planet at the time (before, the change was lost for them and the
  planet kept different T-scores); and the terrain sculpting tools (raise, lower, flatten...) and the planet
  colouring tools change it in the partners' games, at once when they are on the same planet, else when their ship
  lands there (before, only the user saw the new terrain and colours) (BUG-0749, BUG-0883, BUG-0763, BUG-0900).
- Space: buildings bought in a colony's city planner now appear in every player's game. A guest's building is built
  in the host's game (before, it stayed in the guest's game only while the money was spent for everybody), also when
  the host's ship is on another planet or in orbit (it is built once the ship gets there), and a building the host
  or a guest buys appears in every other player's colony city too; such a building stays when a ship leaves the
  planet and lands again, and in the saves (BUG-0751, BUG-0766, BUG-0884, BUG-0901).
- Tribe: on a guest's screen the members now hold what they hold in the host's game: the chief its staff with the
  skull, the others their weapons, instruments and healer's rods. Before, the copies of the host's members often
  stood empty-handed on the guest's screen (the host's chief without its staff) (BUG-0906).
- Tribe: the chief's staff has the same size on the guest's screen as in the host's game; it was about a third bigger
  there (BUG-0912).
- Tribe: a partner's name no longer hangs over empty ground. It was drawn at the spot under the partner's mouse
  pointer, with nobody under it; the partner's name is now only over its chief and its members (and on the arrow at
  the screen edge when the chief is off screen) (BUG-0907).
- Space: sculpting the ground and colouring a planet while no partner is on it now reaches the partners too: their
  game makes the same changes when their ship lands there; before, they kept the old terrain and colours (BUG-0900).
- Space: a colony building a partner bought (built in your game by the mod) is now kept when your ship leaves the
  planet and lands again, and in your saves; before, the game rebuilt the city without it (BUG-0901).
- Space: a guest's drag in a colony's city planner that does not end on a free plot no longer builds an unpaid
  building in the host's game (BUG-0902).
- Space: terraforming a planet (atmosphere, temperature, water) while no partner is on it now changes that planet in
  the partners' games too; before, the change was lost for them and the planet kept different T-scores (BUG-0883).
- Space: the terrain sculpting tools (raise, lower, flatten...) and the planet colouring tools now change the planet in
  the games of the partners on the same planet too; before, only the user saw the new terrain and colours (BUG-0763).
- Space: a building a guest buys in a colony's city planner is now built in the host's game too (when the host's ship
  is on the same planet); before, it stayed in the guest's game only while the money was spent for everybody
  (BUG-0751).
- Space: a colony building a guest buys while the host's ship is on another planet (or in orbit) is no longer lost
  for the host's world: the host's game builds it once its ship is on that planet (BUG-0766).
- Space: a building the host buys in a colony's city planner, and one a guest buys, now appear in every other
  player's colony city too (on their planet, or when their ship gets there); before, only the buyer and the host had
  it (BUG-0884).
- Space: terraforming with a tool (Atmosphere Generator, Ice Comet and the like) now changes the planet in your
  partners' games too. Their game received the change but never applied it to the planet (BUG-0749).
- Space: an NPC empire's store now wants the same spices and pays the same prices in every player's game (each game
  made its own random store: one player sold a Red Spice for 281, the other for 4,779 to the same empire) (BUG-0747).
- Cell: a knocked-out cell no longer swallows meat (or a plant) whole that drifts into its mouth, so it does not
  respawn bigger than "small again" (BUG-0746).
- Tribe: a guest's order to gather a fruit that the host's game no longer has at that spot is no longer ignored: the
  members gather from the nearest fruit tree there (BUG-0744).

### Tests
- The two play tests that failed now and then for no fault of the mod are settled: the Creature bots now really hunt
  the same animal together (they check which creature the game targeted, walk around the partner when it stands in
  the way, and bite together), and the Cell test checks that the host's creatures hunt a guest with a controlled
  trial instead of comparing how often each player happened to be hunted (BUG-0618, BUG-0910).
- Tribe: a test of the duet (both players' members performing for one village fill its social meter faster) (BUG-0748).
- The test lab watches memory: a test script that grows past 2 GB is stopped and fails with a clear line, every
  result records the script's and the games' memory, and a test waits when its games would not fit into the free
  memory. The cause of a run that filled the PC's memory (a read of the divergence detector that never ended in a
  four-player Cell stage) is fixed (BUG-0916).
- The play bots' reach command (`coop.abilities reach`) no longer calls the game on a target that is not a live
  creature (two long tests failed on a fault it caused), `coop.npc list` checks its creatures against the game's
  list, and a new test removes the creature the avatar targets (BUG-0917).
- One report for the session: the host's "Report a problem" also asks every guest for its report, and "Ask the
  players for their reports" in the host's lobby does it any time without the host's own report. Each guest's co-op
  window opens with "The host asks for your report: Send / Don't send" and says what is sent; nothing is sent without
  that click. The reports come to the host over the session and the host gets one file to send,
  `SporeCoop-session-report-<date-time>.zip`, with `Host-<name>.zip`, `Guest-<name>.zip` and a list of who sent, who
  did not, who did not answer and whose report was refused. A report over 25 MB or one that is not a SporeCoop report
  is refused (never unpacked or opened on the host's PC); no player's address goes into the file (BUG-0767).

## [0.7.11.2] - 2026-11-10

### Fixed
- Creature, Tribe: a guest's game could crash now and then when an animal's body was removed by the guest's own game
  (its tribe killed it, a cleanup) and the host's animal then left: the mod removed that body a second time. This came
  with 0.7.11 (BUG-0850).

## [0.7.11.1] - 2026-11-08

0.7.11 was tagged but never published (its release build failed on a test script); 0.7.11.1 is 0.7.11 with that
build fixed. Everything below is new since 0.7.10.1.

### Changed
- Cell: the move to the Creature stage is a vote now. When the host clicks the check in History at the end of the
  Cell stage, everybody gets "wants to move on to the Creature stage: press I'm ready (F9)"; the stage changes when
  everybody is ready, after 60 s or with the host's "Start now". The first creature editor that follows no longer
  asks again: a player who has arrived counts as ready. A guest's own click on the check is its "I'm ready" (BUG-0315).
- Creature: the host's evolution (the mating call) no longer waits for the others: the host's editor opens at once,
  and every guest gets "Evolve now" in the co-op window for 90 s, which opens its own editor whenever it wants (also
  after the host is done). A guest that does not evolve plays on and can evolve at its nest later (BUG-0516).

### Fixed
- Creature and Tribe: a running animal of the host is no longer shown a frame behind where the host has it on a
  slower PC (BUG-0757).
- Update: a short hiccup of GitHub's download server (an answer like 504, a dropped connection, a download that
  breaks off) no longer ends the update at once. The in-game update and SporeCoopSetup.exe try again up to 3 times,
  1 and 3 seconds apart; if it still fails, the message says how many tries were made (BUG-0755).
- The co-op sync self-check no longer reports a MISMATCH (in the log and the "Report a problem" text) while the host
  sends its world to a partner (BUG-0754).
- Tribe: after the host's tribe conquered or allied a village, the host's game showed the village's caption and every
  tribe member stood still (a guest's orders did nothing) until the host clicked it away. With guests in the session
  the caption now closes by itself after 8 s, and everybody is told why the tribe waits (BUG-0531).
- Creature: an animal standing near you is no longer left half in the ground after the host's evolution editor
  (BUG-0752).
- Tribe: the marker the host's game puts at a place a creature attack comes to (and its minimap blip) is now shown to
  every player, also to one who joins later (BUG-0740).
- Space: when both players are on the same planet, the guest now sees the same time of day (sky) as the host (BUG-0712).
- Creature: the eggs your family lays in your own nest are now there for every player, not only for the host (BUG-0660).
- Tribe: food a tribe member was carrying when it died now lies on the ground for every player, instead of
  staying in the hand of that member's copy on the other players' screens (BUG-0661).
- Civilization: an old tribe village the host's vehicles raid now disappears for the other players within about a
  second instead of five (BUG-0659).
- Tribe: a player who joins (or loads the world again) while a dead member of the host's tribe still lies on the
  ground now sees that body too (BUG-0665).
- All stages: F9 pressed in another program (a browser, a chat client) no longer shows or hides the co-op window;
  the social digits and the marker that shows partners where you point read only the game window too (BUG-0713).
- Space: a host ship without a model of its own is no longer drawn on a guest's screen as a copy of the guest's own
  ship; it gets the model of a ship of its empire, or the game's raider model (BUG-0723).
- Space: the ships of the host's empire (its escorts, its colonies' defenders) show the host's own ship designs on a
  guest's screen, not the guest's creation that happens to have the same id (BUG-0724).
- Civilization: a part of a host's vehicle or building design that a guest's game lacks is shown as the most similar
  part that game has, not as one fixed part (BUG-0725); a host's building with such a part is no longer missing from
  the guest's city (BUG-0726).
- Creature: a guest playing its own creature now grows with its brain upgrades like the host's creature (BUG-0732), and
  every player sees the other players' creatures at their real size after a brain upgrade (BUG-0733).
- Creature: the host's pack (with the creatures a guest recruited) defends a guest that an animal attacks (BUG-0735).
- Creature: a guest's Summon Flock is made in the shared world next to the guest's creature (both players see it, it
  joins the party's pack); before, it was made in the guest's own game only (BUG-0736).
- Cell: a guest's bite on a creature now plays the creature's hurt animation and reaction in the host's game, as the
  host's own bite does, and a lethal bite sent by a guest who was just knocked out no longer kills (BUG-0714).
- Cell: a guest's electric part now hurts the host's creatures; before, its zaps showed only on the guest's screen and
  the creature's health came back (BUG-0715).
- Cell: a guest's poison part now makes its poison clouds in the host's game, where the host's creatures meet them;
  before, the guest's own game removed each cloud at once and the host's made none (BUG-0716).
- Cell: a game with very many cells around (over about 2000, e.g. four players at a high level) no longer crashes in
  the game's collision step: the hard limit of the game's merge buffer is guarded now, not only the co-op budgets
  (BUG-0718).
- Tribe, Creature, Civilization and Space: a partner's tribe members, the host's animals, vehicles and ships no longer
  run on the old way through a latency spike when they turn or stop meanwhile (members were shown up to 8-9 m off,
  animals jumped, a ship's copy flew 11 m past where it had stopped) (BUG-0674).
- Creature: the copy of an animal thrown by a hit flies with it over a bad network instead of standing and stepping
  after it (BUG-0675).
- Tribe: over a home connection with latency spikes, or on a busy PC, the host's tribe members are shown where the
  host has them now again, not 0.2-0.4 s behind (BUG-0651).
- Creature: the partner's creature no longer walks on past where the partner stopped during a latency spike
  (BUG-0680).
- Space: a guest now sees the ships the host's game runs (pirates, raiders, NPC empires) fire their beams and
  projectiles; before, they fought without a shot on the guest's screen and only the damage came (BUG-0677).
- Space: a guest's own cargo hold also comes back when the host restarted its game between the guest's leave and its
  join, keeps what changed in it while the guest was away, keeps the guest's own number of cargo slots, and brings back
  plant, animal and artifact cargo the host's copy did not have (BUG-0679).
- Civilization: a guest's game could stall for a moment and log over a hundred caught errors next to an old tribe
  village whose members carried food; the co-op no longer takes food from, or hands food to, the guest's own
  creatures outside the Tribe stage (BUG-0684).
- Creature: after the evolution, while the brain upgrade caption played at the nest, a partner's creature could stand
  half in the ground on another player's screen until it walked; it is put back onto the ground now (BUG-0683).
- Space: a guest's game could hit an error (caught, logged as "SporeCoop caught an error in its own code") right
  after leaving a planet for the galaxy menu; the mod no longer reads the city planner the game has just freed
  (BUG-0682).
- Space: the co-op sync self-check no longer reports a MISMATCH (in the log and the "Report a problem" text) when
  the host's ship brakes or turns while its partner watches it, as in a rescue flight: the partner's copy is compared
  with the path the host's ship flew in the last second (BUG-0547).
- Creature: when the host's planet has creatures the host made, and the guest has made other creatures with the same
  numbers, the guest's game showed its own creature for the host's nests and their cards ("The ... are your
  allies!"); it shows the host's creature now (BUG-0200).
- A guest that plays another world of the host's stage while it stays in the session no longer gets the host's
  evolution vote screen and editor, and keeps its own species relations (BUG-0702, BUG-0703).
- Civilization: an NPC nation's army attacking the same city no longer counts as a second player of a joint siege
  (BUG-0704).
- A player who joins after another left no longer inherits the departed player's Space star (Jump-to), roster and
  budget, or the values and designs the host last sent to it (BUG-0706).
- Creature: a baby animal's health bar on the guest showed the adult maximum of its species (BUG-0710).
- Space: the host's shared money no longer goes into a guest that plays a Space save of its own (BUG-0709).
- Hit impacts and other world effects of a partner are played on your own ground where the two games' grounds differ
  (BUG-0608).
- Tribe and Civilization: food or money a guest earned while the host was loading or in the menu now reaches the
  host's game when it is back; before, the two games' totals stayed apart from then on (BUG-0691).
- Tribe and Civilization: a guest's purchase right after leaving an editor is no longer undone by the host's next
  total (the purchase made and the food or money back) (BUG-0692).
- Civilization and Tribe: a guest's purchase, planner building, gift or superweapon request made while the host is
  in an editor or the menu is made when the host is back, and the guest is told that it waits; before, it was dropped
  without a word (BUG-0693).
- Space: a contract or mission reward tool that arrives while the player is in an editor or an adventure is given
  when the player is back in space; before, it was lost (BUG-0694).
- Space: a contract's time no longer runs out while the host's game is paused or outside Space (BUG-0695).
- Shared food and money: a notice now says which player's purchase or earning changed the shared total (BUG-0696).
- Space: the partner's ship shows its real health on the other screen, not always full (BUG-0697).
- Creature: a change of the species relations (a kill, an impression, an alliance) reaches the guests within about a
  third of a second instead of up to one second (BUG-0699).

## [0.7.10.1] - 2026-11-07

### Fixed
- Civilization: a guest's game could crash about ten seconds into a fight with vehicles near a city whose defence
  turrets fired back; the guest now shows the host's turret shots without a crash (BUG-0688, #198).

## [0.7.10] - 2026-11-07

### Changed
- Civilization: the host's vehicles are drawn where the game is moving them on the guest's screen, no longer a step
  behind (found with BUG-0438).

### Fixed
- Creature: the host's game could crash when the UFO visit (the abduction event) came with a ship whose model its
  game did not have, as with a Sporepedia cache of ship summaries only; seen at the end of the Creature stage. Such a
  ship is now just not drawn (BUG-0686).
- Tribe: when the host's tribe was down to its chief, a hit in a guest's own game on that chief could end the guest's
  game on its own ("Alas! Your population has fallen below the minimum ...", its OK reloads the guest's own save), and
  the guest then no longer saw the host's chief; only the host's game decides now when a member falls (BUG-0685).
- Tribe and Creature: the guest hears the footsteps of the host's epic creatures (BUG-0669).
- Civilization: the guest sees the host's own city buildings (city hall, houses, factories, entertainment) as the
  host designed them, not some creation of its own with the same number (BUG-0670).
- Civilization: the guest sees the host's vehicles as the host designed them, not as some creation of its own with the
  same number (BUG-0647).
- Tribe: the guest's village grows with the host's hut upgrades: it no longer shows "12/6" and can have as many members
  as the host's tribe (BUG-0645).
- Tribe: the member portraits on the right of the guest's screen show the host's evolved creature, no white squares
  (BUG-0643).
- Tribe: the other villages have the same colours for the guest as for the host (BUG-0644).
- Tribe: a guest's game could fault and then stop responding to the co-op for good a moment after it loaded the
  host's Tribe world again (it built one of the host's tool buildings into a tribe of the world it had just left); and
  SporeKit now keeps the mod running after such a caught error even when a menu task was waiting (BUG-0672).
- Tribe: the guest hears the music of the dance when the tribe parties at the fire pit (BUG-0646).
- Tribe: a tribe member the guest buys at its hut always reaches the host's game and belongs to the guest
  (BUG-0671).
- Tribe: the other tribes have the same chief for the guest as for the host; the guest no longer sees a second chief
  with a staff in place of a musician (BUG-0652).
- Space: a player who leaves and joins again gets its own cargo hold back (its tools and their charges, spice and
  cargo), no longer the host's (BUG-0678).
- Joining: a guest whose mouse rested on a planet of the galaxy menu while the host's world arrived no longer stays
  stuck in the menu (it faulted in every frame); a game that faults over and over now writes a report (BUG-0676).
- Creature: a guest who was still in its own Cell editor when the host entered the Creature stage no longer plays a
  creature it once chose in another world; a creature chosen in the co-op window now holds only in the world it was
  chosen (or played) in (BUG-0638).
- Creature: when the species migrates to a new nest, the guest's game migrates too, even without loading the host's
  world: the goal, the "You have claimed this nest" scene and its DNA when the guest gets there, and its respawn at
  the new nest (BUG-0640).
- Creature: a spit now stops a charge between players: your spit stops a charging creature of the host's world, and
  a creature's spit stops your charge (BUG-0641).
- Cell: a guest's kill now happens once in the host's world: biting a dying creature again no longer kills it a
  second time with a second drop of meat (BUG-0632).
- Cell: a guest can no longer eat the same plant or piece of meat twice (it came back for a moment right after it
  was eaten) (BUG-0636).
- Cell: after a level change the cells on the guest's screen no longer all vanish and get replaced; the guest keeps
  what is on its screen as the host does (BUG-0635).
- Long sessions: the game keeps a block of memory for the end of a long session. When Spore (a 32-bit game) runs
  low on memory, it gets that block back and you are told to save and restart, instead of crashing a few minutes
  later (a host crashed in Civilization after two hours of play). Spore keeps every creature, model and texture it
  loads until it is closed, and in co-op the host also loads what lives around the other players (BUG-0631).
- Creature: the other species' nests evolve in the host's game only; a guest's nests show the same species as the
  host's at the same moment, and a nest whose species the host wiped out no longer comes back as a new species on the
  guest's map (BUG-0668).
- Tribe: on a connection with latency spikes or a busy PC the player tribe's citizens no longer trail 2-3 m behind on
  the guest's screen; they are shown where the host has them now (BUG-0410).
- Tribe: a citizen the guest's own game held back no longer falls up to 8 m behind and jumps back; it is kept on its
  path (BUG-0386).

## [0.7.9] - 2026-11-07

### Changed
- Tribe and Civilization: the citizens of the player tribe and the host's vehicles trail less on the guest's screen
  (the guest shows them nearer where the host has them); they can still fall 2-3 m behind on a busy connection, work
  on that continues (BUG-0410, BUG-0438).
- A crash report (`crash-*.txt`) now says to send the one `SporeCoop-report-*.zip` from the same folder (it holds the
  report and its `.dmp`); the `.txt` and `.dmp` alone are needed only when there is no such zip.
- SporeKit keeps running the mod after an error that the game itself catches; before, such an error could stop the mod
  for the rest of the session while the game went on (found with BUG-0667).

### Fixed
- Creature: spending DNA in an editor no longer lowers the other players' evolution bar (the bar could drop to almost
  nothing after both players used their editors, or be halved); a spend now takes only the shared DNA, as in a game
  of one, and the bar keeps everything the party earned (BUG-0630).
- Creature: when the partner stops, its creature on your screen stops where the partner stopped instead of sliding
  on half a metre or more (BUG-0408).
- Creature: the partner standing on a nest mound no longer drops to the ground for a frame each time it comes into
  your view when you turn the camera (BUG-0434).
- Tribe: in the first minute after a player joins, a chief walking past (or stopping next to) another player's chief
  is no longer teleported to the other side of the village, on both screens (BUG-0610).
- Tribe and Civilization: a late network packet no longer makes the guest's tribe members or vehicles jump back to an
  older place for a moment or skips the removal of ones that are gone (BUG-0616).
- A broken position from another player's game (not a number) can no longer be written into a creature, vehicle or
  ship; such a state is dropped (BUG-0614). Messages too big to send are now written to the log instead of being
  dropped without a word (BUG-0615).
- A guard against a crash when an NPC reacted to a creature that had just been destroyed (BUG-0456) was switched off
  by mistake since 0.7.5; it works again (BUG-0623).
- Tribe and other stages: when a game was loaded within a few seconds of the galaxy menu coming up, the menu's
  intro flight could take over the host's view for good (the minimap and clicks no longer moved it); the intro is
  now stopped the moment it starts outside the menu (BUG-0626). The same intro could leave a Creature guest's view on
  the planet seen from space after its join (BUG-0607).
- Civilization: a city the host's game founds instantly (and its vehicles) now appears on the guests' screens at
  once, not several seconds later (BUG-0624).
- Cell: two cells that end up exactly on top of each other no longer shoot off at hundreds of metres per second or
  break a player's own cell for a moment (the game's collision left their push direction unset) (BUG-0307).
- Creature: when the host's game stood still (its death or upgrade caption) and then went on, a guest could lose
  almost every creature around it at once, and they stayed gone while the next caption was up (BUG-0611).
- Creature: creatures the host's game puts away while it stands still (the host's respawn at its nest) no longer
  stay on a guest's screen where they were, as creatures nobody else has (BUG-0339).
- Tribe: a guest no longer sees tribe members carrying food that the host's members already put down (BUG-0617).
- Tribe: when a member of a player's tribe dies, its body now lies on every player's screen as long as it does on the
  host's; before, it vanished at once on the guests' screens (BUG-0620).
- Creature: an animal resting at its nest no longer looks stuck inside the nest mound on a guest's screen when the
  host is somewhere else on the planet (BUG-0562).
- Civilization: after the host takes a city, its buildings stand at the same places on the guests' screens; before,
  a guest's game could tear one building down and put it up again every two seconds a few metres away (BUG-0602).
- Spice geysers are now the same size on every player's screen (a geyser the host's game made during play was ten
  times bigger there than on the guests' screens) (BUG-0485).
- Civilization: the host's vehicles no longer show up ten times too big on a guest's screen (BUG-0549).
- The game no longer risks a crash when SporeKit hooks a function while the game is already running (the first
  test input, the first cell of Cell): the hook is written only once no game thread is inside the bytes it changes
  (BUG-0403).
- Cell: a guest whose pause menu was open while the others played, and who then left and joined again, no longer
  crashes when the host's world is loaded (BUG-0308; the fix came with 0.7.3.2, now tested on this path).
- Tribe: the guest now sees and hears the blows of the host's tribe members land on their victims (the hit flashes
  and their sounds), not only the swings (BUG-0603).
- Creature and Tribe: the host's animals and tribe members no longer trail behind where the host sees them on the
  guest's screen (they were 0.2-0.3 s late, 1-2 m behind a running animal or member): the guest now shows them where
  the host has them at that moment (BUG-0425).
- Creature: a body one player eats is eaten on the other player's screen too, so the guest can eat the bodies the
  players killed and no longer finds bodies it already ate standing whole on the host's screen (BUG-0557, BUG-0383).
- Creature: the guest now sees the UFO that visits the host's world, where the host sees it (BUG-0558).
- Creature: the sea monster that takes a player in deep water rises upright out of the water on the other player's
  screen, as the taken player sees it, instead of tilted into the water; other effects in the world now show at their
  game's orientation and size too (BUG-0465).
- Creature: a rock or stick one player picks up is carried by that player's creature on the other screen too, and lies
  where it landed in both games after it is thrown or dropped (BUG-0458).
- Cell: a player's screen no longer turns to plain water for the rest of the game after a level-up (the camera
  lost its place when the cell's movement glitched for a moment); the view comes back onto your cell (BUG-0604).
- Cell: the end of the Cell stage no longer makes the co-op sync-check report the host's cell hundreds of metres
  off on every guest when nothing was wrong (BUG-0606).
- Creature: while the host lies dead in its death caption (or its game stands still for another reason), a guest that
  walks or respawns somewhere else now sees the host's animals there, instead of an empty spot until the host goes on
  (BUG-0413).
- Creature: a guest that leaves the session keeps the animals around it and plays on with them, and a guest that joins
  again while the host is in its editor no longer finds a planet without creatures (BUG-0412).
- Creature / Tribe: a fruit lying on the ground in the host's game lies at the same place on the guest's screen too,
  also when the guest's own game had rolled it elsewhere or had already lost it (BUG-0497).
- Tribe / Civ: food dropped on the ground and food carried by the host's tribe members is in the guest's game too,
  at the same place, and food a guest's member picks up is gone for the host as well (BUG-0548).
- Civ: a city's cultural targets stand where the host's stand after a guest leaves and joins again (BUG-0540).
- Space: the decorations around a planet's cities are the same in every player's game (the same cities have them, at
  the same places), instead of a different set on each screen (BUG-0541).
- Tribe: the ground round the villages is now the same for every player after the Creature -> Tribe change: the
  host's game had its plateaus up to 14 m higher or lower than the guests' (tribe members and hit flashes there stood
  off the ground on the other screens). For this the host's game loads the new Tribe world once right after the
  change (about 10 s); the guests see "the host is loading the new world once" meanwhile (BUG-0332, BUG-0608).

## [0.7.8] - 2026-11-05

### Changed
- The GOG 2016 build of Spore is no longer supported: please update the game through GOG (the current download
  is the 2024 build). SporeCoop still starts on GOG 2016, but it is not tested there and its bugs are not fixed.

### Fixed
- Creature: the guest's Raging Roar now scares the host's animals: creatures of other species around the guest
  run away in both games, as they do from the host's roar (BUG-0501).
- Co-op window: *Send my world* is greyed out while no game is loaded or nobody has joined, instead of saying
  "Sending the world to everyone..." and doing nothing; the lobby of a host alone says that you can start or load a
  game now and friends can join any time (BUG-0597).
- Co-op window: the "My creature" / "My cell" row says whose creature you play ("the host's species") and, when you
  have none of your own, that making one in an editor gives you the choice (BUG-0598).
- Co-op window: a chat line typed while the host's game was saving the world for a joining player is no longer lost
  (BUG-0551).
- The host's game no longer stays paused on the "game saved" box while the co-op window is open (BUG-0599).
- Tribe: a guest's game should no longer crash when the camera jumps (the home button) to creatures whose effect
  was broken; the broken effect is left out instead (BUG-0575, a guard: the cause is not known).
- Creature / Tribe: the game no longer crashes when a fruit falls from a plant after the other player ate a fruit
  that was on the ground since the save was loaded (BUG-0584).
- Tribe: villages founded during play now show the host's mood and food in the guest's game too, instead of the
  values of another village (BUG-0591).
- Civilization: a city the host's player captured, bought or converted now has the player's buildings in the
  guest's game too, and the other nations' moods and money are the host's (BUG-0593).
- Tribe -> Civilization: the City Hall and vehicle editors of the end of Tribe no longer say "evolved the species",
  and the co-op sync check no longer reports a mismatch while the host's game prepares the Civilization stage
  (BUG-0588).

- Creature: an animal in the air in the host's game (a flier, a jump, thrown by a hit) is in the air at the same
  height in the guest's game too, instead of walking on the ground under it (BUG-0550).
- Creature: your own creature no longer stays half in the ground after the other player comes back at your spot
  (after a return from the main menu or an editor) or bumps into it on a slope (BUG-0592, BUG-0504).
- Tribe: a guest sees the members of every village fight, gather, dance and build, instead of standing still
  (BUG-0569).
- Tribe: a guest hears the stage's hunting and fighting music of its tribe, as the host does (BUG-0568).
- Tribe: a guest hears the fight's sounds of the host's villagers: their war cries going into a fight, their sighs
  coming out of it, the sprint and the effects of their attacks (BUG-0600; the hit impacts of the attack animations
  are still open, BUG-0603).
- Tribe: in co-op new villages no longer appear as soon as the whole party has gathered the food one player's tribe
  needs alone: they come at one player's pace (BUG-0571).

## [0.7.7] - 2026-11-05

### Fixed
- Creature: on a guest with a low frame rate the animals no longer trail metres behind where they walk in the
  host's game (BUG-0590).
- Tribe: the host's game no longer faults every frame when the animals of a creature attack go for a village that
  was founded during play (BUG-0582).
- Tribe: the other villages have the same food in the guest's game as in the host's (BUG-0580).
- Tribe: a guest's game no longer faults every frame when its tribe's leader would take a weapon or instrument a
  member took in the host's game (BUG-0583).
- Creature: an animal's charge now stuns the guest's creature as it stuns the host's (the stars, the stagger), and the
  guest's charge stuns the host's animals too (BUG-0554).
- Creature: animals can no longer bite or charge a guest's creature while it is in the air (BUG-0555).
- Creature: the creatures the host calls with Summon Flock are friendly to the guest too and show their hearts on the
  guest's screen; so are the members of the host's pack (BUG-0556).
- Creature: a nest of a species you allied with shows the ally face on the guest's minimap too (BUG-0561).
- Tribe: the guest sees the red circle under the enemy its selected members attack (BUG-0570).
- Creature: on a game without a parts pack (Creepy & Cute, or any other) the other player's creatures and their
  family no longer show big blocks where the pack's parts are; each part the game lacks is shown as the most similar
  part it has (an eye stays an eye, a foot a foot, of a similar size) (BUG-0559).
- Creature: a creature that kept a paint of the Cell stage (such as the cell stripes) has the same colours on every
  player's screen instead of another shade (BUG-0559).
- Tribe: on a guest whose game runs slowly the animals no longer trail several metres behind where the host's game
  has them while they walk (BUG-0503).
- Tribe: a guest's food no longer stays at 0 for good after its tribe spent the last of it; it shows the host's food
  again, so its members can eat on their own (BUG-0572, BUG-0573).
- Tribe: the villages feel about a guest's tribe as they do in the host's game (hostile after the host's raids,
  friendly after its gifts), instead of all staying neutral (BUG-0565).
- Tribe: a guest's goal card shows the host's current goal village and the host's goal progress (BUG-0564).
- Tribe and Civilization: day and night are the host's on a guest's screen, also after the host's game over and
  reload (BUG-0579).
- Cell: the host's editor after the evolution vote shows the DNA the stage earned instead of 100, and no longer gives
  DNA again on every visit; a guest's Cell DNA counts what the host's editor spent (BUG-0560).
- A guest's game shows the stages the host's game has done (BUG-0577).
- Tribe: after the host's game over and the reload of its last save (or a rejoin) a guest's game no longer keeps the
  villages the host's world does not have: their huts, tools, food mats and map marks are gone too (BUG-0564).
- Tribe: a guest sees an enemy village's hut lose health when the host's tribe attacks it, and a hut or tool damaged
  in the guest's old world is mended to the host's (BUG-0566, BUG-0567).
- Tribe: the co-op epic creature is never one of the players' own creatures from the Creature stage (BUG-0563).
- Tribe -> Civilization: after the evolution vote at the end of Tribe the host's City Hall editor (and then the
  vehicle editor) opens at once instead of after a minute or more on a black screen (BUG-0576).
- Tribe -> Civilization: the host's game no longer freezes in the vehicle editor when a guest's tribe gathers or
  eats food meanwhile (BUG-0585).

## [0.7.6] - 2026-11-04

### Fixed
- Space: a colony a guest founds in the host's home system is kept for it too when it leaves and joins again
  (BUG-0552).
- Civilization: when a guest looks at another city than the host, the people walking there are shown in the
  guest's game; they were all put back at the host's city and the guest's city stayed empty (BUG-0553).

- Space: the game no longer crashes when an NPC empire's ship uses a creation whose model is missing from your
  Sporepedia cache; the ship is shown with a known ship model instead (BUG-0546).
- Civilization: an old tribe village the host's vehicles raided is gone on every guest's screen too (BUG-0539).
- Civilization: a religious vehicle's area of conversion ends on a guest's screen when it ends on the host's, with no
  religious shots left behind (BUG-0530).
- Tribe: the tools of every tribe (the other tribes' fire pits, racks and huts too) now stand at the same places in
  every player's game; a guest's game no longer shows tools there the host's game does not have (BUG-0523).
- Tribe: a tool a guest buys and places now appears in the host's game too, at the same spot, and the guest's
  members can take the weapon or instrument from it (BUG-0524).
- Space: on a colony planet you and your partner now see the colony's citizens standing at the same spots and at
  the same size, and its vehicles driving at the same places (BUG-0490).
- Space: the spice stored in a shared colony's cities is collected once: when one player's ship collects it (by
  arriving in the colony's system), it is gone from the colony and from the spice pickups by its cities on every
  player's planet; before, the other player could collect the same spice again (BUG-0537).
- Civilization: a city the host's vehicles hit now shows its damage on every guest's screen too, not full health
  until a building falls (BUG-0529).
- Civilization: the host's shots (religious ones too) appear on a guest's screen about 0.2 s sooner, nearly at the
  same time as on the host's; a guest's game fires exactly the shots that left on the host (BUG-0530, BUG-0538).
- Civilization: a city's defence turrets are the same in every game: a guest's game no longer builds its own, and
  a turret the host's city builds appears for the guests (BUG-0484).
- Space: a guest's save no longer keeps the partners' ships and the host's ships its game showed; after loading it
  alone they came back as extra Defenders of your own empire (BUG-0522).
- Space: a guest's own colonies are no longer lost when it leaves and joins again: the host keeps them and gives
  them back (with one city; a host that restarts its game does not keep them yet) (BUG-0195).
- Creature: in a guest's social game the creature now shows what to do (it sings, dances ... first) and only an answer
  to its prompt counts, as in the host's own game; before, any social ability at any time counted (BUG-0525).
- Creature: your partner's creature in the middle of a jump or fall no longer drops to the ground for a frame and
  back up (BUG-0532).
- Space: a tool gift or a spice sale to a partner that is still waiting for the answer when you leave the session
  no longer comes back to you while your partner keeps it too (BUG-0542).
- Space: a colony your partner founds in a star system you both own (another planet of your home system, for
  example) now appears in your game too, as a colony of your empire; it stayed a free planet there (BUG-0543).
- Space: a gift, mission, war or alliance your partner makes with an alien empire while the host is in an editor
  is no longer lost when the host comes back to Space (BUG-0544).
- Space: right after the session restarts (a rejoin, a new load) a lethal hit next to your partner disables your
  ship again instead of destroying it (BUG-0257).

## [0.7.5] - 2026-11-04

### Fixed
- A guest in a new world now plays that world's species, not its cell or creature from another game; it plays its
  own creation when it picks it in the co-op window or already played it in this world, and everybody gets a
  notice "<name> plays its own creature" (BUG-0345).
- Creature: when your partner's creature jumps or falls, it now leaves the ground on your screen too instead of
  staying on the ground (BUG-0376).
- Creature: a partner standing on a nest mound is drawn on the mound, not half sunk into it (BUG-0435).
- Cell: after a knockout and the respawn next to your partner, and after your cell is put next to the host after a
  load or an editor, your view is centred on your cell again; it stayed off to the side until you swam a while
  (BUG-0343).
- Cell: while a level-up rescales the world, your partner's cell is drawn where it is; it was drawn several
  metres off for a few seconds (BUG-0519).
- Minimizing the game or switching away from it no longer writes a "WATCHDOG" stack dump into the log as if the
  game had frozen; a real freeze is still reported (BUG-0347).
- Joining a host whose world is sent while your game is still loading no longer sends most of the world twice:
  the transfer resends almost nothing on a good connection and is about twice as fast then (BUG-0405).
- Creature: a guest's game no longer starts its own UFO visit that the host's world does not have, so no UFO
  takes the host's creatures in the guest's game only (BUG-0486).
- Tribe: a weapon rack (or any tool building) the host builds now appears in the guests' games too, a guest's
  members can take the weapon from it, and a guest sees its members hold the weapons and tools they took
  (BUG-0514).
- Tribe: a tribe member sent to take a weapon or tool from a rack no longer stands still next to the rack for
  good when its walk stops short of its place there; it takes the tool (BUG-0505).
- Tribe: the epic creature that comes to the players' village now hits as hard as the game's own epics; it
  dealt only about 8 damage, like an ordinary animal (BUG-0515).
- Tribe: when you save as a guest and later load that save alone, your tribe no longer holds copies of the
  host's members, and the host's villages your game showed are not in it (BUG-0495).
- Tribe: a guest's game no longer makes new villages of its own that the host's world does not have, and the
  members of a village the host no longer has leave the guest's game too (BUG-0483).
- Civilization: the people walking in the cities are where the host's are and have their size on a guest's
  screen, instead of walking somewhere else (BUG-0476, BUG-0475).
- Civilization: roads between cities (trade routes) that appear in the host's game appear in the guest's game too
  (BUG-0494).
- Civilization: a guest sees the host's fights: vehicles and turrets fire their shots in the guest's game too,
  and those shots do no damage of their own there (BUG-0477).
- Creature: a guest's social game now shows the game's social meter, like the host's, and a win tells the
  guest's game that the creature became a friend (BUG-0179).
- A chat line typed in the co-op window while the game's "game saved" box (or another box of the game) is up
  is no longer lost; the window's buttons work then too (BUG-0482).
- Creature: after evolving in the editor your creature is the new species at once, also when you play alone;
  the host no longer saves the game after every evolution in co-op (BUG-0517).
- Tribe and Civilization: the effects of your tribe members and vehicles (a hit, a death, ...) now show on the
  other players' screens too, as in Cell and Creature (BUG-0451).

## [0.7.4] - 2026-11-03

### Fixed
- When Spore itself fails to start (the game closes at once after the mod was installed), you now get a
  message that says what to check and a report to attach, instead of the game just closing (BUG-0521).
- Cell: when the cells grow to the next size, the other cells no longer fly around on the guest's screen for a
  few seconds; they stay where the host's game has them (BUG-0511).
- The game no longer crashes when you open Settings in the galaxy menu or a Cell editor opens (for example from a
  mate) on a PC where Spore cannot make its Documents\My Spore Creations folders, as with the Russian Steam version;
  pictures and videos then go to the Spore folder in AppData (BUG-0506).
- Creature: a guest now sees the host's animals look around, eat, answer and fight as they do on the host,
  instead of standing still or only walking (BUG-0464).
- Creature: a partner whose game froze or was minimized is no longer shown sunk into the ground; you
  are told that the partner is not responding, and when it is back (BUG-0513).
- Cell: a knocked-out cell can no longer bite, spike or kill anything while it waits to be revived
  (BUG-0509).
- Creature: the host's pack no longer turns on a partner when the host targets the partner's creature or
  fights next to it (BUG-0512).
- Creature: a guest no longer crashes when its game removes the partner's creature by itself (seen after
  a long freeze or alt-tab); the partner is shown again (BUG-0507).
- Cell and Creature: after the host's editor a guest in the same world keeps playing: it no longer drops
  to the galaxy menu and loads the whole world again; the host's new creature and (when you play the host's
  species) your own new body arrive in place. A migration to a new nest still sends the world (BUG-0508).
- Creature: a guest can befriend creatures with the mouse too: a click on the social ability's button
  of the ability bar (sing, dance, ...) next to the creature now counts, not only the keys 1-4 (BUG-0518).
- Cell and Creature: a party no longer evolves two to four times faster than a single player: every
  player's DNA (and Cell food) now counts 1/N with N players in the world, so the shared total grows at
  the single-player pace; spending is not affected. Hosts who prefer the old sum set `dna_pace=0` in
  `mods\coop.ini` [coop] (BUG-0510).
- Creature: animals are neutral, hostile or allied to a guest as they are to the host; a guest playing its own
  creature no longer sees neutral species as hostile (BUG-0462).
- Creature: a partner who sneaks is shown sneaking (see-through) on your screen too, and the host's animals
  no longer hear a sneaking guest (BUG-0463).
- Co-op window: a chat line typed while a caption is on the screen (a new part, the brain upgrade, a nest)
  is no longer lost; the line takes the typing and Enter sends it (BUG-0482).
- Creature: after a death your partner's creature appears at the nest and steps out of its egg where its
  own game has it, instead of sliding there or standing a few metres away for a moment (BUG-0499).
- Creature: animals of other species now have the same size on every player's screen; young ones are no
  longer shown at an adult's size on the guest (BUG-0461).
- Tribe: tribe members (yours, your partners' and other tribes') have the same size on every screen; the
  guest no longer sees them bigger or smaller than the host does (BUG-0475).
- Space: your partner's ship is shown at a player ship's size, not at the smaller size of an NPC ship
  (BUG-0479).
- Creature: an animal near a guest no longer stays missing on its screen (or keeps living there after
  the host killed it) when its first message was lost; the guest asks the host for it again (BUG-0487).
- Civ: a guest no longer crashes in its city planner while the host's world goes on (attacks, new
  buildings, vehicles that die): the planner's city is left as it is until the planner closes, and a
  building outside its plot can no longer be picked up (BUG-0491, BUG-0493).
- Civ: a guest no longer crashes when it clicks a city of a nation its game does not know (BUG-0492).
- Civ: a guest's saves no longer keep the host's vehicles it was showing; the save loads cleanly on its
  own (BUG-0470).
- Civ: a vehicle that is gone on the host no longer stays behind as a wreck on the guest (BUG-0478).
- Cell and Creature: the effects of your cell or creature (mating-call hearts, being hit, death, a part
  found, the brain upgrade, ...) now show on the other players' screens too, while they play in the same
  place as you. Tribe, Civilization and Space follow in their own versions (BUG-0451, BUG-0471).
- Creature: a bone or skeleton one player takes is taken for everybody: the party gets one new part, not
  two (BUG-0459).
- Creature and Tribe: fruit one player eats is gone for the other players too, and fruit that falls from
  a tree falls in every game (BUG-0472).
- Creature: a nest whose species the host's game made extinct is extinct for every player (BUG-0460).
- Tribe: the eggs at the animal nests are now in every player's game, not only in the host's (BUG-0474).
- Cell: a part one player collects is now unlocked for the whole party (the host and every guest get
  it in their cell editor, with the mating call), not only in the game of the player who touched it
  (BUG-0467).
- Creature: the parts a guest adds in its own editor of an evolution vote now cost DNA, and the DNA a
  guest spends is taken off the party's total for everybody; before, the guest had the same DNA as
  before the editor (BUG-0466).
- Creature: the host's game no longer crashes when an animal was attacking the partner's creature at the
  moment that creature disappeared (the partner died, left or came back); the animal stops its attack
  (BUG-0453).
- Creature: the host's game no longer aborts ("pure virtual function call") when animals still
  remembered a partner's creature that had just disappeared (BUG-0456).
- Cell: a guest's game no longer crashes when the host comes back from its editor with a new cell before
  the new cell has reached the guest; such cells show up as soon as it arrives (BUG-0455).
- Editor: a skin texture the graphics driver could not make no longer crashes the creature editor
  (RenderWare::Raster::Fill) (BUG-0454).

## [0.7.3.6] - 2026-11-02

### Fixed
- Civ and Space: a partner's vehicle, building or ship whose model uses parts or paints from a pack, a
  mod or another profile's Sporepedia no longer crashes the game that does not have them. Such a model
  is shown with similar base game parts, a vehicle falls back to one of the game's own designs, and a
  building whose design cannot be shown is left out (the log names each unknown part once) (BUG-0469).

## [0.7.3.5] - 2026-11-02

### Fixed
- Creature: a guest whose game lacks a parts pack (for example the Bot Parts pack) no longer crashes
  when it loads a host world whose creature uses a part of that pack: the part is shown as a similar
  base game part (the Creepy & Cute parts first), an unknown paint as a base game paint, and an id that
  cannot be replaced makes the game use its own fallback instead of baking the model. Each unknown id is
  written to the log (BUG-0452).

## [0.7.3.4] - 2026-11-02

### Added
- Problem reports: when a player marks a problem (`/bug <text>` in the co-op chat, `coop.bug`, or F9
  "Report a problem"), the game keeps a screenshot of what that player saw, overlay included (JPEG, at
  most 1280 px wide, a few hundred KB; the newest 5). The report file includes them under `shots/`
  and lists each next to its mark; every player of the session keeps its own screenshot of a mark.
  Crash and hang reports take none of their own moment (not safe there). `[report] screenshot=0` in
  `mods\coop.ini` turns them off and removes the kept ones (BUG-0442). Needs SporeKit 0.3.6.
- Creature and Cell: DNA (and the cell's food) that a partner or the pack earned now says where it came
  from in a short notice: "+9 DNA from Bob's hunt", "+7 DNA shared by Alice's pack", "+10 food (DNA)
  from Bob's meals"; the game logs write each change with its cause (BUG-0389).
- Logs: in the Tribe and Civilization stages the 10 s `trace:` line now says how long the player's own
  pointer has stood still (`ptr_still=`), so a report shows whether the partner's copy of it could have
  caught up yet (BUG-0416).

### Fixed
- Tribe: the game could fault in its physics update in every frame (seen once on a guest, GOG 2024) when
  a physics body had lost its owner, skipping the rest of the physics each frame. Such a body is now left
  out of the update (BUG-0449).
- Tribe: a guest's own tribe members (and the host's) could walk 3-9 m inside the ground, or float over it, on
  the guest's screen when the host walked them up or down a slope in the first half minute after the guest
  joined (the guest's game is still paused then) or while the guest's game was paused; they stay on the
  ground now, and new members appear on it (BUG-0444).
- Creature: after Cell -> Creature the shared creature parts were counted differently in the host's game
  and the guests': the parts a cell brings into the Creature stage (the creature versions of its Cell
  parts) counted only for the host, although every guest's editor offered them too. Every game now counts
  the parts its editor offers, so the party's set agrees after the evolution, a load, a join and a rejoin
  (BUG-0441).
- A game told to host before it reached its main menu (a console line during the start) crashed in the
  host's first time-of-day message: the time of day is now read only once the game has its space player
  data. Players hosting from the co-op window were not affected: the window first shows in the galaxy
  menu (BUG-0433).
- Creature: eggs at the nests of other species could hang in the air on the host: the game lays them
  where the layer stands and an egg that is not touched has no gravity. The host now puts an egg that
  hangs over the ground onto it (BUG-0443).
- Tests: since the busy gate (BUG-0404) an action vote's result and the world share after an evolution can
  wait for a pause or a cinematic on the host. `vote_net_4p` and `party` read the action's result the instant
  the vote ended (it ran 0.9-1.8 s later), `vote` read the guest's body at a fixed time after the evolution
  (its reload came later). The tests wait for them now; the votes and their results were right (BUG-0447).
- Tests: the lab's hitch gate failed a test on the frame that opens an editor (logged just before the
  stage change); that frame now counts as the switch, not as a stall in play (BUG-0448).
- Tests: `editor_desync` failed about every second run in the Cell part after the host's evolution:
  the guest's mating call was pressed while the guest was still reloading the host's world or its
  cell was hatching (the game takes no click then). The test now waits like a player; not a bug of
  the mod (BUG-0411).
- Tests: `puppet_fight` counted steps of the other player's creature that the observer could not see: out of
  the view the game puts a creature on the plain terrain instead of on a nest mound, 1 m lower. Only steps on
  the observer's screen count now; it was not an ability (BUG-0414).
- Creature: an evolution vote that ended while the host's game played a cinematic (the evolution's own
  fade-in right after the last editor, a caption) left the host in an editor without its buttons. The
  vote's editor now waits until the cinematic is over and the game is not paused (the pause menu, the
  Sporepedia, My Collections); a guest's own editor after a vote waits through a pause instead of being
  dropped, and an action vote's result waits the same way (BUG-0404).
- SporeKit: a hook made while the game runs could let a game thread call address 0 (a handled
  fault, once followed by a frozen game): the hook's original function is now set before the hook is
  switched on. Seen in test games when the first simulated input was sent (BUG-0398).
- Creature: the creature parts a player finds (in a skeleton, from a creature, as a reward) are now the
  whole party's, like DNA: a part the host finds is in every guest's editor too, a part a guest finds
  is unlocked for the host and the others ("Bob found a new creature part"), and a guest keeps them
  after a rejoin. Before, each player's game kept its own, and a guest lost its finds at the next
  world load (BUG-0401). Both players need this version.
- Tests: the `team` test could leave the guest's creature 0.94 m under the ground (its move next to
  the host cut through the planet's curve); the test command now puts it onto the ground (BUG-0400).
- Tribe: right after joining, some of the host's animals could stand up to 1.7 m inside the ground on the
  other players' screens for up to half a minute; they are always shown on the ground now (BUG-0399).
- Creature: the host's animals walking downhill no longer keep hopping on the other players' screens
  (a short fall every few seconds) (BUG-0406).
- Creature and Tribe: animals of the host now walk and run on the other players' screens with their
  walk animation, on the host's path: in the Creature stage a moving animal slid over the ground in
  its standing pose, in the Tribe stage animals moved in small steps and jumps (BUG-0361).
- Creature: the other player's creature and the host's animals no longer stand still and then jump
  ahead when they move without walking themselves (carried by an animation): they move along with
  them (BUG-0394).

- Creature: the eggs at other species' nests are the same on every game: a guest now sees the eggs the
  host's nests lay (where the host has them, on the ground), and an egg eaten by either player is gone
  for both. Before, a guest saw none of them (BUG-0384).
- Tribe: the NPC tribes that form during play (the "a new tribe has appeared" event) were there only for
  the host: guests never saw them or their members. A guest now gets each of them where the host has it,
  of the same species, with its members (BUG-0381).
- Tests: the `creature` test's guest hit could land on a member of the host's family or pack, a hit the
  host drops on purpose since BUG-0374 ("hits applied 0 -> 0"); it now hits an NPC outside both (BUG-0390).
- Tests: play_tribe compared the chiefs' line of both games with the host's own counters in it (BUG-0397 still open: other play_tribe checks).
- Creature: a creature of a partner's earlier creature (before its editor) could be missing on the
  guest until the next world share: the host could not send its species any more (BUG-0370).
- Tests: npc_far / npc_pooled walked the host into the sea, where the game's deep water cinematic held
  it in the air (sync-check "p0 10.7" in every run); they now walk on land and check where the guest
  draws the host (BUG-0377).
- Setup: a Spore folder without Galactic Adventures (Steam's base game: `SporeBin`, no
  `SporebinEP1`) now says "Galactic Adventures is not installed" with what to install, instead of
  "choose the SPORE folder"; choosing `SporeBin` or `SporebinEP1` takes the folder above it, and the
  setup also looks in Steam's other library folders (BUG-0391).
- Setup: on Steam, Galactic Adventures (app 24720) has its own folder `steamapps\common\Spore 24720`
  next to base Spore's `steamapps\common\Spore`; the setup now finds it first, and when base Spore
  (or its `SporeBin`) is chosen it says "Galactic Adventures is installed in ...\Spore 24720: using
  that folder" and installs there, instead of saying Galactic Adventures is not installed (BUG-0391).
- Setup and in-game update: when GitHub's API refuses (403 / 429: its hourly limit per IP address,
  hit on VPNs and shared networks) the newest release now comes from the release page, checked
  against `SHA256SUMS.txt` as before; the setup could not install at all. If GitHub does not answer,
  the setup says how to install with the release zip and `install.bat` (BUG-0392).
- Space: in the galactic core's system each player saw the partner's ship 20-30 m above where it
  really was; partners' ships and the host's ships seen on a guest are now drawn exactly where the
  mod puts them (BUG-0378; BUG-0190 still open: NPC ships in the core not measured yet).
- Support sync-check: it no longer reports a difference for a fast Space ship that is only a moment
  behind (the allowed distance grows with the ship's speed), nor for players in the galaxy view,
  where nobody's ship is shown (BUG-0378).
- Civilization: a guest that missed several new cities of other nations (joining, or cities founded
  at once) now gets all of them and their vehicles within seconds; it used to get one city per
  15 s, and those nations' vehicles were missing meanwhile (BUG-0380).
- Civilization: the support sync-check no longer reports a partner's moving pointer as a desync
  (BUG-0380).
- Space: when the host's world cannot go out yet because its ship is on a planet (the Space stage
  saves only off a planet, as right after Civilization), every player is now told that the world
  follows when the host's ship leaves the planet (BUG-0382).
- Cell: a knocked-out cell no longer eats the food that floats into it, so it no longer comes back
  from its respawn bigger than "small again" and the shared food no longer grows while it is out
  (BUG-0364).
- Support: the sync self-check no longer reports a Cell food total that reached a guest a moment
  late, nor the cells at the edge of the 900 a guest is sent in a crowded pool; tests that remove a
  guest's cells on purpose mark it for the lab (BUG-0379).
- Tribe: on a guest's screen, tribe members (the host's and the guest's own) no longer stop and go,
  or jump ahead, when they start or end a walk: they walk smoothly on the host's path, also on a
  network with jitter (BUG-0365). Plays together with 0.7.x; the guest needs this version, and the
  host too for the smoothing over a network with jitter.
- Creature: each player now sees the partner's real health on the partner's creature (it always
  looked full, hits on the partner did not show) and its real max health (BUG-0373).
- Creature: the host's pack and family no longer attack and kill a partner (a partner's hit on a
  pack member made it fight the partner for good); a partner's hits no longer hurt them (BUG-0374).
- Creature: a player who plays its own creature in the host's world now gets the extra health of
  every brain upgrade (it stayed at the first level's health, 14 against the host's 44) (BUG-0375).
- Creature: a guest's own creature editor (after the host's evolution vote) offered every part, also
  the ones the species has not collected yet; it now locks them as the host's editor does (BUG-0368).
- Creature: a guest whose game lacks the Creepy & Cute pack saw the host's creature without the
  layers painted with the pack's paints (black or in other colours); they are now shown as similar
  base game paints in the host's colours (BUG-0369).
- Tools: crash_triage.py named the co-op version of an earlier session (from the crash file's
  summary) instead of the one that crashed (BUG-0372).
- Creature: the partner's creature no longer keeps "jumping" while it walks downhill: the game took
  it for falling for a moment again and again and played its fall animation (BUG-0366).
- Creature: the partner's creature is no longer drawn under the ground: it is now always put on
  your own game's ground when the partner walks or stands on the ground (BUG-0367).

## [0.7.3.3] - 2026-11-01

### Fixed
- Creature: the partner's creature no longer moves in steps and teleports while the partner walks:
  it walks with its walk animation exactly on the partner's path (before it stood or crept between
  restarted walk orders, fell up to 3.5 m behind and was put in place again; BUG-0360).

## [0.7.3.2] - 2026-11-01

### Fixed
- Creature: a guest whose own creature has fewer abilities than the world's creature got an error
  (caught by the game) at every key press, and its ability keys could use the world creature's
  slots: the ability bar was not built again after the guest's avatar took its own creature
  (BUG-0358).
- Space: a player who joined (or rejoined) while the host's ship was landing on a planet could get a
  world saved on that planet: their game then logged errors at every frame and stalled for seconds
  at a time. Such a save is now made again once the host's ship is off the planet (BUG-0357).
- Cell: with four players a guest's game could drop to 3 frames per second at the end of the Cell
  stage and never get into the Creature stage: a crowd of over 256 cells on its screen made the game
  lose soft-body particles every frame until its particle pool was full, and the cells that shared
  the last one were thrown around (BUG-0355).
- Cell: a guest playing its own cell is no longer a "ghost" after joining or a reload (it swam
  through everything and could not eat, bite or be hit): the mod took the rebuilt cell for the
  partner's old puppet (BUG-0342).
- Creature: a player whose game does not have the Creepy & Cute parts pack can now join a host whose
  creatures use its parts: on that player's screen those parts are shown as similar parts of the base
  game (eye for eye, mouth for mouth, the nearest abilities), with a notice; nothing of the pack is
  shared (BUG-0354, BUG-0341).
- Cell: a crowd of hundreds of cells next to a player no longer makes that game fault every frame or
  throw a cell thousands of metres away (the game's pool of cell particles ran full) (BUG-0338).
- Cell: a player who stays in the pause menu while the other players play no longer corrupts the
  game's memory (a broken collision state, then a crash or freeze when Cell ends; seen on the way
  to Creature with four players) (BUG-0348).
- Creature / Cell: the host keeps its world's species; an own creature or cell chosen in an earlier
  session (another world) no longer replaces the species the host just evolved (BUG-0344).
- Cell: after a reload of the world a guest plays its own cell again, as the others see it (BUG-0349).
- Tools: `crash_triage.py` finds the known ticket of a crash in a relocated `SporeApp.exe` (Windows,
  Steam) by its address numbered from 0x400000 (BUG-0346).
- Cell: after the host's world is loaded again (an evolution, "Send my world"), a guest's cell no
  longer starts alone up to 130 m from the host: it joins the host as after the first load
  (BUG-0352).
- Cell: a guest that bites a creature of the host now sees its health ring, as the host does
  (BUG-0350).
- Cell: the log names the food, the DNA spent and the editor's DNA budget at every cell editor and
  stage entry, to explain reports of unexpected DNA (diagnostics, BUG-0351 still open).
- Tribe: at the arrival and after a rejoin, a player's chief that stands in another one is moved to
  its own spot round the village; two chiefs that end up in each other later are moved apart
  (BUG-0340 still open: the chiefs can still start in each other in some runs).
- Cell: a game with more than 1024 cells in one column of the cell physics (seen on a guest of a
  four-player game, 0.7.3.1) no longer faults every frame with a half-second hitch each, a stopped
  simulation and a frozen game after the stage change (BUG-0337).
- Creature: an animal hit or killed while it runs now shows its health and its death on the other
  players' screens at once (BUG-0334).
- Creature: a player who leaves and joins again (or loads) within a minute of being saved by the
  pack is saved again in the new session; the rescue cooldown no longer carries over (BUG-0330).
- Tribe: where the village ground is higher or lower for the other players than for the host, the
  tribe members and animals no longer walk in place or jump around on their screens: they stand on
  their own ground where the host's stand (BUG-0329).
- Civilization: a vehicle of another nation standing on a spice node no longer flickers 13 m away
  on the other players' screens (their game moved it off ground it found invalid), and a fast
  vehicle is no longer thrown ahead of its place when the host stutters (BUG-0186).
- Civilization: a building the host's game rebuilt after a siege no longer stays a ruin for the
  other players (BUG-0324).
- Creature: animals running on the host are no longer shown metres behind on the other players'
  screens (their copies hardly walked: each new walk order restarted the walk); a running animal's
  copy now follows the host's path exactly (BUG-0281).
- Creature: an editor opened right after a rejoin on a slow PC no longer stays black for good: it
  opens once the player's creature is built (BUG-0318).
- Space: a guest's game no longer faults when the host's NPC ships use a ship model the guest does
  not have (the empires' ships come from each player's own Sporepedia cache); the guest shows such
  a ship with a model of that empire it has, or not at all (BUG-0323).
- Space finale: a host who waited at the galactic core and went in first is counted as entering
  together (a counter the tests read; players saw nothing wrong, BUG-0320).
- Co-op window: a host whose PC is on the network twice (cable and Wi-Fi) is listed once, not
  once per address (both hosts need this version; BUG-0210).
- Creature and Tribe: a partner's creature, an animal or a citizen that cannot keep up by walking
  no longer falls further behind on a player whose game runs at a low frame rate (the pull towards
  the host's position was per frame, now per second; BUG-0175).
- Tribe: leaving the Tribe stage (for example for the galaxy menu before loading another save) no
  longer faults when the game sends a tribe member it has already cleaned up on one more walk; that
  fault could leave the next load from the menu unfinished (BUG-0310, BUG-0285).
- Civilization: a vehicle the host's game moves in one step (not by driving) no longer flies past
  its place on the other players' screens for a moment (BUG-0186).
- Cell: a player leaving the Cell stage to load the host's world (after joining or rejoining) no
  longer has the mod create or move cells while the game is tearing the stage down (a guard after one
  crash seen in testing, BUG-0308 still open).
- Cell: a player whose cell was moved far again and again (for example put back after an invalid
  position) no longer fills its game with leftover background cells; a guest then stopped seeing
  the host's cells at all. Background cells far from the player are now removed (BUG-0314).
- Creature: when the host leaves to the main menu, the other players keep the animals they saw
  around them (their own game runs them now); before, their planet stood empty and the nests
  brought their members back one at a time (BUG-0180).
- Creature: a player's game checks every second that the copies of the host's animals are still in
  its world and makes a lost one again, with a log line saying what happened (diagnostics, BUG-0300
  still open).
- Tests: a test reusing running games no longer inherits fixes switched off by the previous test
  (BUG-0312); `coop_legless_guest.py --guest cancel` clicks the red cross as it says (BUG-0317).
- Space: a star nobody had visited could still get different planets in the two games (for
  example 6 and 10), so a partner's planet did not exist on the other side. The game loads the star's
  species on the way and that load drew from the same random numbers as the planets; it no longer
  does (BUG-0185). Test: `coop_space_seed_profile.py`.
- Tribe (and Creature): an animal running in a herd, for example one fleeing a hunt, no longer lags
  a few metres behind on the other players' screens: a copy pushed off its path by the animal beside
  it is now put back on it after half a second (BUG-0305).
- Space: players joining while the host is on a planet no longer end up on that planet when the
  host leaves it (their ships went back down a few seconds after loading the host's world while the
  host stayed in the solar system): the world is now saved 2 s after the host is in the system, also
  when the save would have gone out in the very frame the ship left the planet (BUG-0301, BUG-0287).
- Tests: a host that fails to start hosting is logged and retried at the start of a test (BUG-0302).
- Cell with three or four players: the games no longer fault and crash when the players spread
  out (the game's own collision step holds about 2000 cells; the host kept the area around every
  player alive and the pools grew past it). The host now fills the other players' areas only while
  it holds under 1100 cells (1300 for what that player sees), each player gets at most its 900 nearest cells, and the self-check no
  longer reports the cells around other players as missing (BUG-0295, BUG-0296, BUG-0297,
  BUG-0298). Test: `coop_cell_4p.py`.
- Cell: a player whose own cell ends up at an invalid position is put back where it was, and its
  game no longer fills the world from that position (a crash at the next level-up, BUG-0299).
- Cell: a player far from the host found no food and no other cells: the host's game kept only
  the area around its own cell alive. Now it populates and keeps the area around every player, and
  each player gets the cells around itself, so two players exploring apart can both eat (BUG-0174,
  BUG-0279).
- Cell: a cell whose position is invalid is no longer sent to or shown by the other players (a
  guard after one crash seen in testing, BUG-0292).
- All modules now use one rule for "is the host's world the one I play", and the host names its
  world when a player joins, so a guest joining in the host's Civ world no longer misses the first
  vehicle and city state (BUG-0231).
- A world share that failed after the host's save ("cannot share the world: ...") is retried up to
  3 times, 30 s apart; a guest in another world no longer stays there for the whole stage
  (BUG-0231).
- Space: a trade or spice gift that comes back after a galaxy reload is paid only into the game it
  was taken from: kept for that game while another one is loaded, paid when the same game was saved
  since, dropped when the reloaded game still has it or its save slot was replaced by a received
  world (BUG-0225, GitHub #159).
- A rare fault in the game's UI right after a guest loaded a shared world (a destroyed modal window
  left in the window manager) is caught before the next mouse message (BUG-0270).
- Tests: `space_rivalry` waits until both ships are on the home planet before its checks (BUG-0262).
- Tribe: after the host's tribe was wiped out and the host bounced back in time, the other players
  no longer lose the host's whole tribe for the rest of the session (BUG-0288).
- Cell: while the host is in the main menu, an editor or a loading screen, a guest now plays on in
  its own tide pool (the creatures swim, eat and are eaten, new ones appear) instead of a frozen
  one, and the food it eats meanwhile goes to the shared progress when the host is back; then the
  host's pool takes over again (BUG-0205, BUG-0151). Test: `coop_cell_away.py`.
- Creature: a guest far from the host no longer loses the animals around it. The host's game kept
  its world alive only around its own camera: farther than about 150 m it put the animals back
  into its pool, also next to a guest, and nest members there appeared and vanished every few
  seconds. The host now keeps the world alive around every player (BUG-0280).
- Tribe: the animals where a guest looks, far from the host's village, now live on the host's game
  and reach the guest (the host kept and sent them only around its own village) (BUG-0280,
  BUG-0291).
- Creature: a guest that rejoined and loaded the host's world could fault every frame in the herd
  update (a herd of the save with an empty member slot); such slots are dropped now (BUG-0256).

### Changed
- A guest that plays another world of the host's stage (joined from its own game, loaded one of its
  own saves, or kept its world with `auto_load_world=0`) now plays that world on its own: **DNA it
  earns there stays in that world and its save** and no longer goes to the host's world; when it
  then loads the host's world, its DNA becomes the host's total (the DNA earned meanwhile does not
  join the pack). Its money and tribe food stay its own there too, and a spend made in the shared
  world just before it left still counts once. Cities founded in such a world by 0.7.3.0 stay in
  the guest's save (BUG-0231).

### Tests
- The test monitor no longer reports a crash when a game's console is only busy with another
  command of the test (BUG-0261); `coop.tribe act` refuses hut and tool actions on a tribe member,
  which made the host's game fault (BUG-0260); the campaign's coverage finding names the missing
  objects per kind.
- Cell: `coop_cell_editor_timed.py` enters the own cell editor right after a real respawn or revive
  (BUG-0237: the stage is left ~10 s after touching the mate, the invulnerability is over by then,
  not reproduced); `coop_cell_respawn.py` also checks the death count (BUG-0221, not reproduced).
- Three and four players per stage in minutes: `tools/tests/party.py` (the helper for every test with
  more than two players) and `coop_party.py` in the new `multi` suite: two guests joining at once,
  each guest's game showing the others, shared totals, a vote with four, a leave and rejoin of the
  last guest, the host in the menu with several guests, a player joining during a vote; and one
  test per stage for the gameplay between guests: knockouts and revives (Cell), a joint hunt, one
  NPC impressed by two guests and noticing two partners (Creature), the tribe split and chiefs with
  four (Tribe), ownership, gifts, trade, roles and two superweapon requests (Civ), two players with
  the same name, gifts, a spice sale, a contract crew without the host, allies mirrors, rivals and
  rescue between guests (Space), leaves during another guest's transfer (BUG-0152).
- civ_reload and civ_sw_vote assert the game's own state (the boost speed, the order a vehicle took,
  the money a superweapon cost) and fail with their fixes switched off (BUG-0222).
- The campaign bot answers a captured city's specialty box before a nation's talk under it, and
  closes a talk that does not take its answer (BUG-0290); `select` waits for the partner to be on
  the screen (BUG-0272).
- The Space play bot no longer clicks under the rank badge (it opened the captain screen, which
  takes no mouse wheel), and the galactic core finale closes a captain screen before zooming out:
  a guest of the 4-player campaign stayed next to the core (BUG-0303, test `space_captain`). The
  captain screen's caught fault at 0x5971D4 is the game's own, also without the mod (BUG-0304).
  `coop_sync_multi.py --fixture tribe` measures the Tribe stage with four players.

## [0.7.3.1] - 2026-10-29

### Added
- Problem reports: type `/bug <what you see>` in the co-op chat when something goes wrong; every
  player's log gets a line with the text and the moment, and the chat says "marked for the report".
- Problem reports: the games of a session share one clock, so the logs of all players line up
  (`tools/merge_logs.py` merges them into one timeline). The host and the guests compare the shared
  world every 30 s and log what differs (`sync-check:`, `[coop] sync_check=0` turns it off). The log
  also has the frame rate and hitches, the game's free memory, the network quality per player, the
  saves and world transfers with their times, settings that are not the defaults, and a summary
  line of each session (also at the top of the report). The report lists other mods, extra
  `.package` files and game files of an unexpected size. Paths of your PC are masked in logs and
  reports ([support guide](docs/REPORTING.md#reporting-support)).
- Problem reports: the log now says what each player did and what the mod decided: a state line
  every 10 s (stage, world, position, partners and their distance, why a partner is hidden, link
  quality, the food on the screen in Cell, the creature's body and where it came from) and lines
  for eating, bites, bodies, votes, clicks in the co-op window, knockouts, stage changes, joins and
  leaves, about 0.5 MB an hour. `[coop] log_detail=0|1|2` in coop.ini (`coop.log detail N` in the
  console) sets how much. "Report a problem" reminds that the other players should send theirs too.
- Every game logs the SporeCoop, SporeKit and game versions of the other players when they join
  (and warns when they differ); `coop.status` lists them. Older versions show as unknown.

### Changed
- Logs and problem reports no longer contain other players' internet addresses: a public address
  is shown as a short tag ("public #3F2A"), a home network one by its class ("192.168.x.x").

### Fixed
- A load from the galaxy menu (a stage jump, a received world) could crash the game in
  `cStarRecord::GetPlanetRecord` when the stage was left with a game fault that kept the old
  world's player empire in the menu: the mod now clears it first (BUG-0284).
- Wine: the game could crash right after leaving a session, while the co-op window looked up the
  PC's network addresses (the "Friends connect to" line); Wine's network code returned garbage when
  several games of one Wine prefix asked. The lookup now runs in the background and rarely, does
  not use that part of Wine at all under Wine, and a fault in it no longer ends the game
  (BUG-0286).
- Votes: "I'm ready" in the co-op window did nothing while a caption of the game was on the
  screen (the brain-upgrade captions at the end of the Creature stage): the vote ran out of time.
  The window's buttons now work during captions and cinematics (BUG-0273).
- Stage changes: after the change to Tribe the guests could load a Creature save of the host and
  stay in Creature, and in Space their ships could go back down to the home planet, until they
  rejoined. The host's save for them now waits until it has been 2 s in the new stage or place, a
  save the game did not write is made again, and older files are no longer sent as the new save
  (BUG-0276, BUG-0287).
- Creature: when the host's game put away NPCs far from the host's creature, a guest standing
  near them still saw them for 2 s (again with every respawn of a nest member there). The host
  now removes such an NPC from the guests at once (BUG-0274).
- Tests: the four-player sync check no longer calls a whole kind of objects out of sync for one
  object off among a few, and allows for the guest's designed 0.15 s lag; measured per player, the
  3rd and 4th player were as close to the host as the 2nd (BUG-0275).
- Creature: a new guest (no creations of its own) that saved an edit in its own editor at the start
  of the Creature stage could be stuck with the host's legless creature from the Cell stage, on its
  own screen (and after "Replace" on the host's too), after every rejoin and restart. Only a
  creature the guest really made becomes its own now, and the co-op window's arrows can go back to
  the world's own creature (BUG-0278).
- Cell: the host's (or a guest's) game could crash while the Cell stage ended and the Creature
  planet loaded, when the co-op status or the player's position was asked for at that moment. The
  mod no longer reads the cells the game has already freed, nor anything else of a stage the game
  left since the last frame (BUG-0271).
- Creature: a guest's game could fault a second after it started to leave the stage for a world
  the host shared (all three guests of a 4-player game, right after the host's creature editor).
  The mod no longer touches the player's creature while the game tears the stage down (BUG-0264).
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
  until it plays the host's world (BUG-0231, partly: the shared resources and DNA of such a guest are still open). Cities already founded that way stay in the
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
  address** (BUG-0225, the allies part; refunds after a galaxy reload are still open).
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
