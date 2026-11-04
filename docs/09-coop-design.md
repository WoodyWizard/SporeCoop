# 9. Concept: playing together, not side by side

This chapter is the session mode **Together**. The second mode, **Rivals** (every player with a
base and a faction of its own, a race through every stage), is designed in
[chapter 11](11-rivals.md); the host picks the mode per stage in the lobby (11.2).

Before this version every player played in their own copy of the host's world, and only resources
and visibility were shared. The new goal: **one world, one team**. All players are in the same
world and affect it, each has their own role and their own property, and the best results come
only from working together.

## 9.1 Three rules for all phases

1. **One world, simulated by the host.** Everything that moves and lives (creatures, tribe members, vehicles,
   ships) is simulated only by the host's game. Instead of their own copies, guests have
   "mirrors" of the host's objects (like the NPCs in the Creature phase today). So the worlds never diverge.
2. **Everything has an owner.** An avatar, a tribe member, a city, a vehicle, a ship belongs to
   a specific player. A guest sends the owner's commands to the host, the host executes them, and everyone sees
   the result. You cannot command someone else's property, but you can see it (owner's label and color).
3. **Together is better.** Every phase has mechanics that only work when players are
   close to each other or act at the same time: assistance, revival, joint attacks, shared progress.

## 9.2 Phases

### Cell - "Symbiosis"
* One tide pool: NPC cells, food and predators are shared (simulated by the host, mirrored on guests).
* **Bond.** If players' cells are close (≤ 6 m), both regenerate health faster.
* **Knockout and revive.** A lethal hit (if a partner is in the same pool) does not kill but
  stuns the cell for 10 s: it freezes and is invulnerable. A partner who swims right next to it
  (≈ 3 m) and stays there for 1.5 s revives it with half health. If they fail, the
  cell respawns next to the partner but **small** (half size); it
  grows back by eating (50 food points - full size) or at the next level.
  Progress (evolution bar, parts) is not lost. Alone in the pool - normal Spore death.
* **Hunting together.** Damage to a large cell adds up, food is shared by everyone.
* A shared evolution bar (like DNA).

### Creature - "Pack"
* Already there: one planet, shared NPCs, fights via the host, shared DNA.
* **Revive.** If a player dies and a partner is nearby, the partner can pick them up
  (walk up and hold a social action / key), otherwise they respawn at the nest.
* **Joint hunting.** If two players hit the same target, each one's damage is +25 %.
* **Joint socializing.** Any player's social actions advance friendship with the same
  nest (counted by the host) - with several players, befriending is easier.
* **Epics** are stronger but give more DNA: they are nearly impossible to beat alone.

### Tribe - "Chiefs"
* One tribe, shared food and huts. **Tribe members are split between players**: the host has their own,
  each guest has their own (equal shares by default; new members go to whoever "bought" them).
* Everyone selects and leads **their own** members: hunting, gathering, attacking, music.
  Guest commands go to the host, and members move identically for everyone.
* Roles emerge naturally: one hunts and feeds, another fights, a third makes friends.
* Buildings and new members are bought from shared food; whoever bought it owns it.
* **Working together pays:** a *pincer* (members of two players hit the same tribe within 10 s)
  deals +50 % damage; every player has their own **chief** (bigger, tagged "<name>'s chief"):
  members near it hit +20 %, and a chief about to fall next to a partner's member is saved once a
  minute; a *duet* (both players perform for the same tribe) fills the social mini-game faster;
  a healer who fully heals a partner's member brings 5 food; every 10 minutes an **epic creature**
  comes to the village: 80 food if both players fought it, 30 otherwise. Members can be given to
  a partner (F9 window or `coop.give`).

### Civilization - "Governors"
* One civilization, a shared treasury. **Cities are split between players**: the capital goes to the host,
  the rest are handed out in the lobby, and a captured or bought city goes to the player whose
  vehicles took it.
* In **their own** city a player builds buildings and buys vehicles. In someone else's they cannot
  (hint: "Alice's city").
* **Vehicles** belong to the buyer, and only the buyer drives them. Joint attacks: a city
  is captured faster if vehicles of several players storm it.
* Diplomacy (alliances, trade, religion) is the host's; guests make suggestions via ping and chat.
* **Roles** (F9 window, `coop.role`): *military* - own vehicles hit buildings 25 % harder;
  *economic* - trade between players' cities pays more; *religious* - own preachers convert 50 %
  faster, twice as fast while another player's army besieges the city.
* **Joint siege:** a city hit by vehicles of two players within 10 s falls 50 % faster.
* **Alarm:** a player city under attack is marked for everybody (label and edge arrow), and the
  other players' vehicles nearby go 50 % faster for 20 s.
* **Superweapons** fire only after a vote of all players. Vehicles and cities can be given away.

### Space - "Squadron"
* One empire, a shared budget, shared colonies. Everyone has their own ship.
* **Wing.** Ships in the same system: enemies and pirates are shared (simulated by the host),
  guests' shots hit the same targets.
* **Colonies.** Anyone can found a colony; it is shared. Defending colonies from raids
  is easier with two: one fights off pirates, the other repairs/delivers.
* Freedom: you can scatter across the galaxy, but missions and wars pay off better together.
* Every player flies in their own copy of the galaxy (same seed); these link the copies:
  * **Rescue:** a lethal hit while a partner is in the same system only *disables* the ship for
    20 s (no shooting, "SHIP DISABLED" on screen, a "REPAIR <name>" beacon for the partner);
    the partner repairs it by flying within 60 m for 2 s, else the native death and respawn follow.
  * **Jump to partner:** travel straight to a partner's star system (F9 window, `coop.space jump`),
    free and instant, also into systems nobody visited yet: the planets load, the empire there meets
    you (comm, first contact) and both games generate the same planets for a new star.
  * **Raids:** a raid on a colony alerts the partners (with the jump offer); a raid beaten with a
    partner in the system pays 2000 more.
  * **Missions:** a completed mission gives its reward tools to the partners too; done with a
    partner nearby: +25 % money.
  * **Terraforming together:** partners on the same planet change both copies of it.
  * **Cargo:** give spice to a partner (F9 window: "Give 10"); without a free slot it comes back.
* Not possible with separate saves: shared colony *ownership* (each save keeps its own colonies).

### Space 0.7 - "A galaxy together" (in progress, 2026-10-23)

Two (to four) rulers of one galaxy, each with their own territory, who see each other, help each
other, trade and finish the game together. Every feature is a file of its own on top of the
registry `mods/coop/space_ext.h` (MSG_SPACE kind ranges, frame tick, the F9 "Space" page rows,
`coop.space <feature> ...` test commands). The features and the data they share:

| # | Feature | File | Kinds | What the player gets |
|---|---|---|---|---|
| 1 | Partners on the galaxy map (done) | `space_map.cpp` | 10-19 | a "▼ Bob" tag in the partner's colour on its star in the galaxy view, or on its ship while it flies there; an arrow on the screen edge when it is outside the view; a click on the tag (or "Fly there" on the Space page, "Bob: <star>") jumps there for free (to its system; next to its ship in the galaxy view) |
| 2 | Partner colonies on the map (done) | `space_map.cpp` | 10-19 | a dotted ring in the partner's colour around every star with a colony of the partner's own (the colonies both started with are shared), its name next to it when few are in view; the Space page shows the colonies one at a time (planet, star, spice, cities) with "Next" and "Fly there" (the game's flight price, 0 in this game; no range limit: a partner's colony is a friendly port) |
| 3 | Tool gifts | `space_trade.cpp` | 70-79 | give charges of consumable tools (bombs, terraforming charges, colony and city tools, monoliths) to a partner from the F9 Space page ("Antimatter Bomb x12 [Give 5 to Bob]", "Next tool", "Other partner"); what does not fit (the partner's tool is full) comes back. Permanent tools (weapons, drives, scan) and adventure / archetype tools are not giftable (docs/re/space.md 14.1) |
| 4 | Partner colonies as an allied empire | `space_allies.cpp` | 60-69 | in your galaxy a partner's own colonies (those not in your copy) exist as cities of "<name>'s empire", an ally (an enemy when the players are at war in rivals mode): its stars, a city to land at, a Space page row "Fly there"; a raid on such a colony can be fought there in your galaxy and the win defends the partner's real colony. Never saved: taken out before every save, gone when the session ends |
| 5 | Trade between players | `space_trade.cpp` | 70-79 | at a star with a partner's colony the Space page offers "Bob's <colony> buys Red Spice $9000 (2x market)" [Sell 10]: twice the market value (NPC stores pay 0.6-3x for the 3 spices they want, 5% for the others), paid by the galaxy into the shared budget (an export bonus), the spice lands in the partner's cargo hold; a colony buys 30 units per 10 minutes and not its own spice. Rivals mode: an offer at 1.5x that the partner accepts and pays from its own budget (docs/re/space.md 14.2-14.3) |
| 7 | Co-op contracts | `space_contracts.cpp` | 20-29 | jobs made for two, offered by the host every 10 minutes and shown on the F9 Space page ("Accept", "Jump there"): a **pirate armada** heading for a colony of the host (two waves, the game's own pirate raids with tougher ships, that come only while two ships of the crew are on the colony) and a **joint survey** of 2-3 nearby stars (the whole crew at each star together); a time limit; the reward (money once to the shared budget, or to each player in rivals mode, plus a weapon or gear tool for each member of the crew) |
| 8 | Shared diplomacy | `space_diplomacy.cpp` | 30-39 | the players are one people for the NPC empires: a first contact, a gift, a mission, a trade, an attack, an alliance, a war or a peace by one player changes every player's copy the same way, with a notice ("Alice formed an alliance with ..."); the host's relation table keeps the copies equal (every 20 s, on joining and with the "Resync" button) |
| 9 | The galaxy core together | `space_finale.cpp` | 40-49 | done: partners are told when someone approaches the core (Grox space) or reaches its gate, "Fly to <name>" takes you to a partner who is there; the core waits until everybody is at the gate ("Enter now" to go alone); partners at the gate enter together (the Steve cinematic on every game), everybody else gets Steve's gift (the Staff of Life with 42 charges, achievement "42"); a partner who joins later sees that the core was reached |
| 10 | Rivals mode | `space_rivalry.cpp` | 50-59 | a host option on the F9 Space page ("Mode: Allies [Switch to rivals]"): every player is an empire of its own with its own budget (the shared money is split equally on the switch and summed on the way back); on the Space page each player sees the others as ally / neutral / at war and can propose an alliance (the other accepts), declare war (one-sided, everybody is told) or propose peace (the other accepts); ships of players at war damage each other with their real weapons (native death and respawn, an enemy never rescues) |

Shared contracts between the features:
* **Roster** (`space_map.h`, kinds 10-19): every player broadcasts, every few seconds and on a
  change, a summary of its empire: home star, current place, empire name and colour, and its
  colonies (StarID, PlanetID, planet name, spice). `roster_of(id)` / `roster_ids()` serve
  features 2, 4, 5, 7 and 10. A rejoining player gets the others' rosters within one period.
* **Rivals** (`space_ext.h`, owned by `space_rivalry.cpp`): `rivals()` (the host's option, sent to everybody) and
  `relation(a, b)` (ally / neutral / war between two players). Shared money, shared diplomacy and
  allied colonies follow it: in rivals mode money is per player and diplomacy is per player;
  a partner at war shows its colonies as an enemy empire.
* Star and planet ids are the same in every copy of the galaxy (the same save: StarID =
  `starRecord+0x70`, PlanetID = `record+0x184`, docs/re/space.md 7.3). Political ids of NPC
  empires and of the player's empire are the same too (docs/re/space.md 12.3); their names and
  species are not (each copy picks its own at the first contact).
* Every feature works on both supported builds (symbols in `overrides_922af7c7.sym` and
  `overrides_dc04aee5.sym`; `tools/re_lint.py` fails otherwise) and has a two-game test
  `tools/tests/coop_space_<feature>.py`.

### Adventures
* Everyone shares one adventure (guests auto-launch the host's adventure), with a party of captains.

## 9.3 Phase transitions - "Vote"
* When the phase's shared progress is complete, **everyone** gets a window "Move to Tribe?
  Ready: Alice ✔, Bob …". The transition starts when everyone is ready or after 60 s.
* The host goes through the editor and the cutscene, then the world is sent to guests automatically.
* A guest cannot advance alone: their transition button leads to the vote.
* An editor within a phase (for example, the creature editor after the mating call) does not kick guests out.

## 9.4 What already works (verified by automated tests on two game instances)

| Phase | Done | Module | Test |
|---|---|---|---|
| All | Evolution vote: the host's request to enter an editor (new phase or evolution within a phase) waits until everyone presses "I'm ready", 60 s, or "Start now"; once the host enters the new phase the world is sent to everyone ("evolving together"); after an editor inside a stage a guest in the same world gets only the new creation (BUG-0508; a migration still sends the world); a guest's editor session is not interrupted | `coop.cpp` (`h_Submit`, `on_mode_change`) | `coop_vote.py` |
| Cell | One pool: the host's NPCs and food appear on the guest as passive mirrors, the guest has none of its own; guest bites are applied by the host (DamageCell, the lethal BreakOrSwallow and mouth chunks); kills, their death and their meat happen on the host and are seen by both, the guest's game drops no loot of its own, food from a chunk counts once (docs/re/cell.md 11); the host's NPCs face on the guest as on the host (the soft body is turned, docs/re/cell.md 10); bites by the host's NPCs on the guest's cell are sent to the guest; shared food counter; "Symbiosis" - a 10 s knockout (visible to all: translucent cell, "KO n", revive bar, off-screen arrow to the partner, game effects), revive by a partner right next to you, otherwise respawn next to them as a small cell (grows by eating); healing near a partner; the partner's cell is exactly in place and faces the same way (pose is set right after the simulation step) | `cellsync.cpp` | `coop_cell.py`, `coop_cell_spin.py`, `coop_cell_meat.py` |
| Creature | Joint hunting +25 %, "The pack protects" (40 % health instead of death, once a minute), regeneration near a partner; NPCs, fights, DNA shared (existing); kills die on every screen and count for the hunt on both; NPCs near a partner treat it as the player (the game's AI knows only one) | `teamplay.cpp`, `npc.cpp` | `coop_team.py`, `coop_two_instances.py`, `coop_creature_kills.py` |
| Tribe | All members of all tribes are host mirrors; the player tribe's members are split between players; you can select and command only your own; guest orders (move, attack, hunt, gather…) are executed by the host; animals around the tribe are shared; owner labels | `tribe.cpp` | `coop_tribe.py` |
| Civilization | All vehicles are host mirrors; cities are split (the capital is shared while there are fewer cities than players); a vehicle belongs to its buyer; guest purchases, orders and buildings go through the host; host buildings and captures are replayed on guests; owner labels | `civ.cpp` | `coop_civ.py` |
| Tribe (co-op) | Pincer +50 %, own chief per player (aura, protection, tag), duet, healer food, epic creature at the village | `tribe.cpp` (co-op section) | `coop_tribe_play.py` |
| Civilization (co-op) | Roles, joint siege, alarm with beacon and speed boost, superweapon vote, gifts of vehicles and cities, trade between players | `civ.cpp`, `coop.cpp` | `coop_civ_play.py`, `coop_civ.py` |
| Space | In the host's system/on the host's planet the NPC ships are the host's, the guest has mirrors (the guest's own NPCs there are removed); guest shots are applied by the host; NPC hits on the guest's ship are sent to the guest; mirrors do not fire on their own | `squadron.cpp` | `coop_space.py` |
| Space (co-op) | Rescue of a disabled ship, jump to partner (into any system, from a planet, a system or the galaxy view; the same planets in both games), raid alerts and bonus, shared mission reward tools, terraforming together, spice gifts | `spacecoop.cpp` | `coop_space_play.py`, `coop_space_jump.py` |
| Space (contracts) | Co-op contracts from the host: a pirate armada on the host's colony (two waves of the game's own raids, started only while two crew ships are there; the host's NPC ships, so everybody fights the same ships; "Land" on the page) and a joint survey of nearby stars; offer, accept and "Jump there" on the F9 Space page, notices, a HUD line, a time limit, failure on a timeout / an unanswered offer / a partner leaving, money and tool rewards on every game | `space_contracts.cpp` | `coop_space_contracts.py` |
| Space (rivals) | Host option "rivals": own budgets (fair split, merged back), alliance / war / peace between players by buttons on the F9 Space page, PvP by real weapons between players at war (an enemy's ship belongs to no empire in your game; hits go to its owner), rejoins keep mode, relations and budget | `space_rivalry.cpp` | `coop_space_rivalry.py` |
| Space (diplomacy) | One people for the NPC empires: first contacts, relationship events (gifts, missions, trade, attacks) and war / alliance / peace by one player are replayed in every copy through the game's own functions; the host's relation table is sent every 20 s, on entering Space and with the F9 Space page "Resync" button; off in rivals mode | `space_diplomacy.cpp` | `coop_space_diplomacy.py` |
| Space (galaxy map) | The roster of every player's empire (home, place, empire, colonies; `space_map.h` for the other features); partners' tags and their own colonies on the galaxy view, hidden under menus, comm screens and cinematics; a click on a tag or "Fly there" on the Space page travels there; rosters come back after a rejoin | `space_map.cpp` | `coop_space_map.py` |
| Space (trade) | Gifts of tool charges from the F9 Space page (consumable tools only, the rest comes back when the partner's tool is full); spice sales to a partner's colony at the star (roster of space_map.h) at twice the market value, paid into the shared budget, the spice lands in the partner's cargo; rivals mode offers (written, untested) | `space_trade.cpp` | `coop_space_trade.py` |
| Space (allies) | A partner's colonies founded after the join appear in your galaxy as an allied empire "<name>'s empire" (stars, colony city, F9 "Fly there"); its raids can be fought from your galaxy (a win completes the partner's raid); the mirror is taken out of every save and removed at the session end | `space_allies.cpp` | `coop_space_allies.py` |
| Space: the galactic core | Partners near the core / at its gate are shown and called, "Fly to" a partner there (real clicks), the core waits for the partners, joint entry with the cinematic on both games, the Staff of Life for everybody, the session goes on afterwards, a rejoin sees the core reached | `space_finale.cpp` | `coop_space_finale.py` (`--alone`: "Enter now" and the gift by message) |

Not done yet: shared colony ownership in space (separate saves; analysis in docs/re/space.md §5, §7), NPC civilizations' city growth
on the guest still runs locally (the host's buildings do arrive, though), auto-launching the adventure on guests.

## 9.4.1 Your own creations

Each player has their own creature, cell and ship (`mods/coop/creations.cpp`,
analysis - [docs/re/creations.md](re/creations.md)):
* your creation is taken from the player's "My Creations" (`EditorSaves.package` in their profile): for a guest
  the newest one by default, for the host - the species of their world, the ship - their own empire's ship. In the F9 window
  the line "My creature: Kirgle < >" switches it; the choice is stored in the profile
  (`Spore/SporeCoop/choices.ini`);
* the creation's records (`bem` + metadata) go to the host, which distributes them to everyone, including new players. On
  every machine the creation gets the same new ID (player + content hash: creation IDs
  in different profiles collide), is written to a small package and attached to the
  game's resources on the fly (`DatabasePackedFile` + `RegisterDatabase`). The host's creations are also registered under their
  original ID above the guest's "My Creations" - that is how the guest gets the host world's species;
* a player's copy is built from their creation; your own avatar gets your own creature
  (`SetSpecies`: health and abilities from its parts) - 3 s after the world loads; your own
  cell - the player's model + a level rebuild (the cell hatches again next to the partner);
* evolution vote: the host and guests open the editor for **their own** creature; a host world
  that arrives while the guest is in the editor is loaded after the guest leaves the editor; after the editor
  the edited creature (or cell) becomes the player's own and is sent to the others, always with its
  records (a new creation is sent once it is in the My Creations file: [re/creations.md](re/creations.md) 8);
* Cell stage: a guest calls its own mate (the mating-call button) and evolves its own cell in its
  own editor without a vote; the host's mating call goes through the vote (one vote per call), and
  after the host's editor everybody gets the host's new cell in place (no world reload since BUG-0508,
  [re/creations.md](re/creations.md#creations-evolve-in-place))
  ([re/cell.md](re/cell.md) 10, `coop_editor_desync.py`).

In the own editor the parts of the player's own creature are allowed even when the stage editor's
part list lacks them (Creature Creator parts): otherwise "done" would stay in the editor
([docs/re/creations.md](re/creations.md) 7).

Not ready: the player's ship is visible to others but not tested separately; tribe and
civilization members use the host world's species.

## 9.5 Implementation order
1. Transition vote, editor fix, owner colors/labels. (all phases)
2. Creature: revive, joint hunting, shared socializing.
3. Tribe: mirroring host members on guests, owners, guest commands via the host.
4. Civilization: mirroring vehicles, city and vehicle owners, buildings via the host.
5. Cell: mirroring NPC cells, bond, rescue.
6. Space: shared enemies in the system, guest shots via the host, shared colonies.

Each step is verified by an automated test on two game instances. Status is in the README.

## 9.6 Ideas from players (backlog, not planned yet)

Collected from player comments (Reddit, 2026-10-24) and sorted by what they need. "Builds on" is
what SporeCoop already has; the effort is a rough guess from what is known about the game
(docs/re/*.md). The key limit behind most of the hard ones: every game runs **one** stage at a
time, and players only see each other when their stage and place match (the start of docs/07-modes.md).

| Idea | Builds on | What is missing | Effort |
|---|---|---|---|
| Friends help to clear a stage faster | shared DNA / food / money, host-authoritative NPCs, joint bonuses (9.2) | mostly there; possible extras: a "contribution" line per player, a faster-together bonus | small |
| PvP in the Creature stage | space rivals mode (`space_rivalry.cpp`: war between players, hits relayed to the victim, native death) | partners' creatures are invulnerable puppets; the same war switch and hit relay for creatures (TakeDamage needs a non-null direction, docs/re/creature.md), a respawn rule, no rescue between enemies | medium |
| PvP in the Tribe / Civilization stage | members and vehicles already belong to players (tribe.cpp, civ.cpp), host-authoritative units | attacks on another player's members / cities by orders through the host, war / peace between players (the rivals storage of `space_ext.h`), captured cities changing owner | medium to large |
| "Dominate the planet" race | the above PvP, shared world | a scored match mode on top of Tribe / Civ PvP: a goal (all cities / all tribes), a timer, a result screen | medium after PvP |
| Visit another player's planet to help or sabotage | world transfer (the host's save goes to guests), allied colonies in Space (0.7) | a "visit" that loads a partner's own save for a while and brings everyone back, or in Space: rivals at war may attack the mirrored colonies (today a mirror is not synced back to its owner) | medium (Space), large (other stages) |
| Mixed stages: Creature vs Tribe vs Civilization on one planet | puppets, NPC mirroring | the games run different simulators and scales; one player would have to appear in another's stage as something that stage knows (a creature player as an epic in Civ, Civ vehicles as creatures' enemies). Research first | very large |
| Abducted by a Space player and dropped on another planet | the abduct tool (docs/re/space.md 0: ufo +0x7A4), stage switches through the galaxy menu | a Space player and a Creature player in the same world at once (mixed stages, above) | very large |
| Walk through your city in the Civilization stage | Adventures have a walking captain; Civ has none | the Civ stage has no creature avatar on foot; spawning and controlling one there is unknown ground | very large / research |
| Play as a rogue or an epic creature | own creature from My Creations (`SetSpecies`), epic spawning known (docs/re/tribe.md E: `isEpic`, archetypes) | the player's avatar with the epic archetype or size and health, balanced against the partners; NPC reactions to it | medium |
| Balance: no wiping out other players from Space | rivals mode limits PvP to ships | any planet-level attack on another player (bombs, planet busters) needs rules or a switch before it exists | design note |

PvP in every stage, "dominate the planet" races and separate bases are now designed as the
Rivals mode ([chapter 11](11-rivals.md)). Other good next candidates: play as an epic, and the Space
"sabotage" part of visits (rivals at war attacking mirrored colonies). The mixed-stage ideas need a
research spike first: can one game show a puppet of a player who is in another stage?
