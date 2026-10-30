# 11. Rivals: own bases in one world (design)

Status: **design, not implemented** (decided with the project owner, 2026-10-25). Chapter 9 is
the other session mode, "Together" (one team, one base). This chapter is the second mode,
"Rivals": every player has a base, a species, a tribe, a nation and an empire of its own, all
players live in the same world, everybody always sees where the others are, and every stage is a
race. Players can be neutral, allies or at war with each other.

## 11.1 Decisions

| Topic | Decision |
|---|---|
| Mode | A second session mode next to "Together" (chapter 9), chosen **per stage** in the lobby (11.2) |
| Relations between players | Neutral at the start; an alliance (proposed and accepted), war (declared), peace (proposed and accepted), as Space rivals mode does today (`space_rivalry.cpp`) |
| War | Units, members and creatures can be killed, food stolen, cities and colonies captured. The **home is inviolable**: the nest, the main village, the capital and the homeworld cannot be captured or destroyed. Nobody is knocked out of the game |
| Alliance | Players can give and sell resources to each other (DNA, food, money, spice, tools). An alliance can only be broken with a warning: war starts 60 s after the notice. The co-op bonuses of chapter 9 (rescue, pincer, joint hunt) are **not** part of an alliance |
| Wild species, tribes, nations, empires | Every player has **its own relation** with every NPC faction (two players can both be friends of the same species). Players use **influence** to turn an NPC faction against a rival or to protect their own relation (11.4) |
| Influence | Earned by peaceful deeds (social successes, gifts, missions, trade) and bought with resources at a rising price |
| Stage end | The first player to meet the stage's condition wins the stage and starts a catch-up timer for the others; when it runs out (or everybody is done) everybody moves on together (11.5) |
| Tribe / Civilization condition | More than half of the NPC tribes / cities subjugated (allied, conquered, converted or bought). The rival is not needed. When no free NPC is left, the one with the most wins |
| The ones behind | A **draft**: a player who did not win picks a few things the winner has, as copies (the winner keeps them); the winner is paid in resources for every pick, more for every further pick (11.6) |
| Bases | A lobby setting "Near / Middle (default) / Far"; the places are real nest, village and city sites of the planet |
| Visibility | Always: name tags, edge arrows, marks of the rival's base, units and cities on the map, a "Race" page with everybody's progress |
| Cell | Selectable like the other stages. Rivals: one tide pool, a race for the evolution bar, biting each other at war, no NPC diplomacy |
| Space | The host's nation keeps the home planet; every other player's nation **emigrates** to a similar planet in a nearby system with as many cities (11.3.5) |
| Overall winner | The "Chronicle": points for every stage and a big bonus for the first at the galaxy core; the most points wins (11.7) |

## 11.2 The lobby: a table of stages

The host picks the mode of every stage before the game starts:

```
 Stage          Mode                    Presets: [All together] [All rivals] [Custom]
 Cell           ( ) Together (o) Rivals
 Creature       ( ) Together (o) Rivals
 Tribe          ( ) Together (o) Rivals
 Civilization   ( ) Together (o) Rivals
 Space          ( ) Together (o) Rivals
 Catch-up timer [5 min]   Draft picks [3]   Bases [Middle]
```

* Every player sees the table in the lobby; the host changes it. A stage's mode can still be
  changed until the stage starts: the transition window (the vote of 9.3) shows the next stage's
  mode, and the host can switch it there (everybody sees the change before pressing "Ready").
* Mixed games are allowed: Cell together and Creature rivals means everybody leaves the shared
  pool and gets a nest of its own at the start of the Creature stage. Rivals then Together means
  the factions merge: the host's faction takes the others' members, cities or colonies (they keep
  their owners as in chapter 9).
* The table is part of the session state (sent on join, kept in the host's save data, 11.8).

## 11.3 The stages

Every stage below lists: what each player owns, what is shared, what war and alliance change, how
the stage ends, and what the others see.

### 11.3.1 Cell - "The race to land"
* One tide pool simulated by the host, as today (chapter 9: NPC cells and food are host mirrors on
  the guests). Food is physical: whoever eats a chunk first gets it.
* Every player has **its own** evolution bar and DNA (today the counter is shared).
* Neutral or allied: players cannot hurt each other. War: bites hurt the rival (the host applies
  them, as for NPC cells), a kill drops the victim's meat; the victim respawns as in vanilla.
  Knockout and revive (Symbiosis) are Together-only.
* No NPC diplomacy and no influence in the Cell stage.
* End: the first whose bar is full wins. The others get the timer.
* Seen: the rival's cell with its name, an edge arrow, its bar on the Race page.

### 11.3.2 Creature - "Rival species"
* **Own species** (the player's creation), **own nest** and **own family**: a nest of the
  planet is taken over for each player who is not the host, its herd becomes the player's species,
  and its members are the player's family (simulated by the host, mirrored on the others). Its own
  family also greets its hatchlings after a respawn (docs/re/creature.md 13).
* Own DNA, own brain level, own collected parts.
* Every wild species has a relation with each player (impressed / allied / hostile / killed).
  Extinction is shared: a species hunted to extinction is gone for everybody.
* Allies of a player join **its** pack. An NPC species that is hostile to a player attacks that
  player on sight (the AI already notices a partner's creature as the player: docs/re/creature.md 14).
* Influence: "Turn <species> against <rival>" moves that species' relation with the rival one step
  towards hostile; "Protect my bond with <species>" blocks that for 10 minutes.
* War: a player can attack the rival's creature and its family. Deaths respawn at the own nest.
  The nest itself cannot be destroyed (its eggs and members respawn as in vanilla).
* End: the first to reach the Tribe stage's native condition (the last brain upgrade) wins.
* Seen: the rival's creature with its name, an arrow, its nest on the minimap; the Race page:
  DNA, brain level, allies, extinct species.

### 11.3.3 Tribe - "Rival tribes"
* **Own tribe**: the host's tribe is the host's; for every other player an NPC tribe of the planet
  is taken over (at the base distance), its members become the player's species and its chief the
  player's chief.
* Own food, own huts and tools, own members (bought from own food).
* Every NPC tribe has its own relation with each player tribe (allied, neutral, hostile,
  conquered).
* Influence: "Turn <tribe> against <rival>", "Protect my alliance with <tribe>"; earned by music
  and gifts that worked, bought with food at a rising price.
* War: members can fight the rival's members, raid its village for food and burn its huts; the
  main village cannot be conquered (its last hut stands, members can be bought again).
* Alliance: food and tools can be given or sold.
* End: the first to subjugate (ally or conquer) more than half of the NPC tribes wins. When no free
  NPC tribe is left before that, the one with the most wins.
* Seen: the rival's members and chief with its colour and name, its village on the map, an edge
  arrow to its chief; the Race page: food, members, tribes allied / conquered.

### 11.3.4 Civilization - "Rival nations"
* **Own nation**: the host's nation is the host's; for every other player an NPC nation is taken
  over (its capital at the base distance).
* Own money, own vehicles, own cities (bought, captured, converted).
* Every NPC nation has its own relation with each player.
* Influence: "Turn <nation> against <rival>", "Protect my treaty with <nation>"; earned by trade
  routes and missions, bought with money at a rising price.
* War: vehicles can attack the rival's vehicles and cities; ordinary cities can be captured
  (they change owner). The capital cannot be captured or converted. Superweapons cannot target the
  rival's capital.
* Alliance: money and spice can be given or sold; trade routes between allied players pay both.
* End: the first to own more than half of the NPC cities wins; when no NPC city is left free, the
  one with the most cities wins.
* Seen: the rival's cities and vehicles in its colour with its name, its capital on the map; the
  Race page: money, cities, vehicles, NPC nations allied.

### 11.3.5 Space - "Rival empires"
* The **host's nation keeps the home planet**. Every other player's nation emigrates: in its game
  its empire's homeworld becomes a similar planet in a nearby system (same type and size where
  possible), with as many cities as the nation had at the end of the Civilization stage. A short
  message explains it ("Your people left <planet> for <new planet>").
* Each player flies in its own copy of the galaxy (as today, chapter 9 Space): the other players'
  empires appear in it as rival empires with their stars and colonies (the mirror of
  `space_allies.cpp`, as an enemy at war).
* Own budget, own colonies, own tools (Space rivals mode, `space_rivalry.cpp`, becomes this
  stage's rules). Relations with NPC empires are per player (shared diplomacy is off).
* Influence: "Turn <empire> against <rival>", "Protect my alliance with <empire>"; earned by
  missions and trade, bought with money.
* War: ships fight with real weapons (exists), colonies can be attacked and captured; the
  homeworld cannot be attacked by planet busters or captured.
* Alliance: tools, spice and money can be given or sold (exists: `space_trade.cpp`).
* The stage has no end; the **galaxy core is the finale**: the first player to enter the core gets
  the Chronicle's finale bonus (`space_finale.cpp` already tells everybody who is near the core).
* Seen: partner tags and colony rings on the galaxy map (exist), the Race page: money, colonies,
  stars, NPC empires allied, distance to the core.

## 11.4 Relations, influence and NPC diplomacy

* **Player relations** (one table for the whole game, kept across stages): neutral, allied, war.
  A proposal (alliance, peace) waits for the other player's answer; war is declared one-sidedly and
  everybody is told. Breaking an alliance: "Alice ends the alliance: war in 60 s". With more than
  two players every pair has its own relation.
* **NPC relations per player**: every NPC faction (wild species, NPC tribe, NPC nation, NPC empire)
  has a relation with each player, computed by the host from that player's deeds. Today the game
  keeps one relation per NPC faction (with "the player"); the host keeps one table per player and
  writes the right one into each game (the Creature stage already sends the host's table to the
  guests: docs/re/creature.md 8.1).
* **Influence** (a number per player):
  * earned: a social success with an NPC, a gift, a completed NPC mission, a trade route; a small
    amount over time while allied with an NPC faction;
  * bought: with the stage's resource (DNA, food, money) at a price that grows with every purchase
    in the stage;
  * spent: **Turn against** (an NPC faction's relation with a rival goes one step towards
    hostile; it cannot be used on a faction the rival is allied with while that bond is
    protected), **Protect** (an own relation cannot be turned for 10 minutes), **Smooth over**
    (an own relation goes one step up).
  * Shown: the Race page has the influence, the actions and their prices.

## 11.5 How a stage ends

1. The host checks every player's condition every second (own bar, brain level, share of NPC
   tribes or cities).
2. The first player to meet it **wins the stage**. Everybody is told ("Bob wins the Creature
   stage! 5:00 left to catch up"), the timer runs on every HUD.
3. Others who meet the condition before the timer ends finish second, third, and so on.
4. When the timer ends or every player is done, the transition window opens (the vote of 9.3,
   with the next stage's mode, 11.2) and then the draft (11.6).
5. Everybody moves on: the host's game goes through its native transition, and the guests get the
   new world (as today). A player who did not finish moves on with what it has, plus its draft
   picks.

The Space stage has no end (the finale is the galaxy core, 11.7).

## 11.6 The draft: copies from the winner

* After a stage, every player who did not win may take a few copies of things the winner has and
  it lacks; the winner keeps its own. The lobby sets the number of picks (default 3); a player
  who finished second takes one pick less, a player who finished in time but third or later too.
* What can be copied, per stage:
  * Cell: a cell part (mouth, movement or weapon part) the winner had unlocked;
  * Creature: a part the winner had collected;
  * Tribe: a tool or a hut type (instrument, weapon, healer's hut);
  * Civilization: a vehicle or building type the winner had (by the winner's nation's style);
  * Space: not used (no stage end).
* For every pick of a copy the winner gets resources of the next stage at its start: the first
  pick pays a base amount, every further pick (from any player) pays more (for example 100, 150,
  200, ... units of the next stage's resource). So the winner gains from being copied.
* The draft is a window with the winner's items, each with "Copy"; a player can skip; it ends
  after 60 s or when everybody is done.
* The stage's consequence card (carnivore, herbivore, social, ...) is each player's own, from its
  own play style, as in vanilla.

## 11.7 The Chronicle (score)

* Points per stage: the win, the place (second, third), the NPC factions won (allied or conquered
  species, tribes, cities), achievements of the stage, wars won (a peace signed after capturing).
* Space: points for colonies and allied empires, and the **finale bonus** for the first player to
  enter the galaxy core.
* The Race page shows the Chronicle; the end screen after the core (or when the host ends the
  game) shows the totals and the winner. The game goes on afterwards.

## 11.8 Shared systems (all stages)

| System | What it does | Builds on |
|---|---|---|
| Stage table | the mode per stage (11.2), sent on join, saved with the world | the lobby (08-ui), the vote (9.3) |
| Ownership | which NPC faction (herd, tribe, nation, empire) belongs to which player, kept by the host, saved with the world, restored on load | tribe.cpp / civ.cpp ownership of members, cities and vehicles |
| Perspective | every game shows **its own** player's faction as "the player" (the avatar's nest, the player tribe, the player nation, the player empire), the others as factions of the world | Creature avatar of the player's own creature (creations.cpp), separate saves in Space, "whose world is this machine in": `host_world.h` / `host_absent_for()` ([07-modes](07-modes.md#modes-host-world)) |
| Per-player progress | DNA, food, money per player (today shared: `shared.cpp`) | shared.cpp, the DNA decision table `dna_route.h`, the ledger hold `SharedLedger::pause()` ([07-modes](07-modes.md#modes-host-world)) |
| Player relations | neutral / allied / war, proposals, warnings, all stages | the rivals storage of `space_ext.h` (`rivals()`, `relation()`) |
| NPC relations and influence | one NPC relation table per player on the host, written into each game; influence | the Creature species table sync (social.cpp, docs/re/creature.md 8.1), space_diplomacy.cpp |
| PvP | hits between players at war applied by the host (members, creatures, vehicles) or the victim (ships), home protection | the Space PvP hit relay (docs/re/space.md 11.2), the damage filters |
| Markers | names, colours, edge arrows, base marks on the map in every stage | tags and arrows of chapter 9 (cell, creature, tribe chief, civ alarm, galaxy map) |
| Race page | F9 page with everybody's progress, the timer, influence and its actions, the Chronicle | the F9 Space page (`space_ext.h` rows) |
| Stage end | conditions, winner, timer, the forced transition on the host when a guest won | the vote (9.3) |
| Draft | the copy window, payments | creations.cpp (parts), tribe / civ tool unlocks |

## 11.9 What needs research first

The mod already has host-simulated NPCs, mirrors, ownership of members, cities and vehicles,
puppets of the players' own creations, per-player Space empires and PvP between ships. What is new
and unknown:

* **R1 Perspective swap.** A guest's game must treat its own nest, tribe or nation as the
  player's (its UI, cards, controls, the game's notion of "the player") while the host's game
  simulates everything. Creature: the avatar's herd and nest (the guest's avatar is already its own
  creature; the herd and nest are not). Tribe: the player tribe (`GetPlayerTribe`). Civilization:
  the player civilization and political id (`GetPlayerCivilization`). The question per stage: can
  the guest's game switch "the player" to another faction of the same save, and what breaks.
* **R2 Taking over an NPC faction on the host.** A nest with its herd (swap the herd's species to
  the player's creation), an NPC tribe (stop its planner AI, `cTribe +0x290`, docs/re/tribe.md 7),
  an NPC nation (stop its AI, take its capital). Choosing the site by the base distance.
* **R3 Per-player conditions and native transitions.** Computing every player's stage condition on
  the host; ending the stage on the host's game when a guest won (the host has not met its own
  native condition): find the native transition call per stage.
* **R4 NPC relations per player.** Where each stage keeps the relation with "the player"
  (species table: known; tribe relations; nation relations; empire relations: known,
  docs/re/space.md 12), and writing a per-player copy into each game.
* **R5 Emigration to Space.** Setting a guest's empire homeworld to another planet of its galaxy
  copy, with cities (the colony and city tools of space_allies.cpp build "<name>'s empire" cities
  already), and keeping the host's planet in the guest's copy as the host's empire.
* **R6 The draft items.** Unlocking a part, a tribe tool or a civ vehicle type for a player by
  code; giving the next stage's resources at its start.
* **R7 Home protection.** Blocking a capture of a nest, main village, capital or homeworld while
  keeping fights around it.

## 11.10 Order of work

Each step ends with two-game tests (three or four players later) and the docs; a stage is only
offered in the lobby once its step is done.

1. **Shared systems** (11.8): the stage table, player relations for all stages, the Race page,
   markers, per-player progress, the stage end with timer, the draft window, the Chronicle.
2. **Cell rivals**: per-player bars, PvP bites at war (little research: the pool is host
   authoritative today).
3. **Creature rivals**: R1 (avatar herd and nest), R2 (nest takeover), R4 (species table per
   player, influence), PvP between creatures, R3.
4. **Tribe rivals**: R1, R2, R4 for tribes, raids, home protection.
5. **Civilization rivals**: R1, R2, R4 for nations, captures between players, home protection.
6. **Space rivals**: R5 emigration, homeworld protection, influence with NPC empires (Space rivals
   mode is mostly there).
7. Balance: draft prices, influence prices, timer length, base distances, from play tests.

## 11.11 Open points (decide while building)

* The exact numbers: draft payments, influence prices and gains, the 10-minute protection, the
  60-second warning, the timer.
* Three or four players: every pair has its own relation; can allies form teams that win a stage
  together (not chosen for now: every player wins alone)?
* What a player who is far behind for several stages gets besides the draft (none planned).
* Rivals in Adventures (Galactic Adventures): not planned.

## Guest colonies that survive a rejoin (2026-10-30, fix/w13-space)
<a id="rivals-guest-colonies"></a>

Design note for BUG-0195 (left open in the 0.7.3.2 patch). Today every player flies in a copy of
the host's save: a colony the guest founds exists only in the guest's running game and is gone when
it rejoins (it loads the host's world again). The host shows it meanwhile as a city of the mirror
empire "<guest>'s empire" (`space_allies.cpp`, docs/re/space.md 15), which is undone before every
save, so the host's save never holds it either.

Why it was not fixed in the patch (not a contained change):

* The obvious fix, "the host founds the guest's colony in its own world at record level" (the
  `coop.space empire colony` path: star claim, `cPlanetRecord_SetTechLevel(rec, 5)`,
  `FillPlanetDataForTechLevel`, `cEmpire_AddPlanet`), collides with the allies mirror: the mirror
  claims the same star for the mirror empire and writes the old fields down, the save hook restores
  them before every save, so an adoption made after the mirror is undone by the next save, and one
  made before it makes the star "owned" (no mirror, but then the colony is the host's, not the
  guest's: in rivals mode it would be wrong).
* The planet record must exist on the host: a star the host never visited has no records until
  `RequirePlanetsForStar`, and its planets can differ (BUG-0185).
* The guest's colony state (cities, stored spice) lives only in the guest's game; the host would
  make a fresh colony with one city and no spice.

What Rivals 0.8 needs (fits the Ownership row of 11.8):

1. The host keeps **a colony list per player** (player identity as for the owed refunds of
   BUG-0225: world epoch + save key), filled from the rosters (`space_map.h` `own_colony`), saved
   next to the world (a side file, never inside the game's save), restored on load.
2. On a guest's join, the host sends the list; the guest applies each colony as its **own**
   record-level colony in its copy (its player empire, after `RequirePlanetsForStar` with the host's
   planet records, BUG-0185's sure fix) before its roster is first sent.
3. The host keeps showing them as that player's mirror empire (allied, or at war in rivals mode),
   so the save hook stays the only place that strips them from the host's save.
4. Spice and cities: the guest's roster already carries cities and the spice key per colony; the
   apply sets them (`set_spice` of `coop.space empire colony`).
5. Conflicts: a planet the host (or another player) colonised in the meantime stays with whoever
   has it in the host's world; the guest's entry is dropped with a message.

Test: `coop_space_map.py` already founds a guest colony and checks that it is gone after the rejoin
(the line to turn around).
