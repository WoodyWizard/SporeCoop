---
id: BUG-9135
title: Tribe / Civ: with three or more players no partner gets a village of their own
status: open
kind: feature
stage: Tribe Civ
reported: 2026-11-11
source: owner report on 0.7.8-lineages53-dev ("Started the save, but they still don't have their own tribes when launching")
role: host
versions: 0.7.8-lineages46-dev to 0.7.8-lineages53-dev (by design since the founders were added)
fixed_in:
branch: feat/lineages-47
commits:
tests: tests/civ_seats_tests.cpp (unit, Civ seat rules), tests/tribe_seats_tests.cpp, tests/tribe_late_tests.cpp (unit, Tribe seats and late joiners), tests/win_tests.cpp (founders::raise_now / raise_seats, seats added mid-stage)
signature: host log has no "tribe automatic founders" line at all; session has 3 players (host + 2 partners)
docs: docs/re/civ.md#civ-multi-founders
related: BUG-9040 BUG-9134 BUG-9054 BUG-9056 BUG-9073
switch: tribe.auto_founders, civ.multi_founders, tribe.multi_founders, creature.own_nests, tribe.late_prompt, tribe.late_catch_up, tribe.late_create_editor, civ.late_capital
---

## Report
The owner hosted "Jiff Peanutbutter.spo" (Tribe) with two partners (Kittykath and BigTexas, players 1 and 2). Neither
partner got a village of their own. The host log has no founding line, not even a refusal.

## Cause
The automatic founders (tribe.cpp auto_founders_tick) only run with exactly two players
(`C.sess->players().size() != 2` returns before anything is checked or logged). The whole founder design is for one
partner: founders::Roster saves slots 0 and 1 only (founders.cpp refuses a roster with slots 2 or 3 filled), Outfits
has two seats, and about 40 places in tribe.cpp use `g_order_roster.guest()` (orders, alliances, damage, rescue,
performance, the village commander). Several Tribe features also check for exactly two players (tribe.cpp 2010, 2078,
4608, 4631, 4790, 4907; civ.cpp 5700). Civ founding has the same two-player limit.

## Needed
One founder seat per partner (up to four seats, as the campaign seat rules already allow): the roster's save format,
founding a village for each partner without one, every `guest()` use mapped from the partner's player id to their seat,
the village commander per partner, and the same in Civilization. Until then the session should at least say so instead
of doing nothing.

## Workaround
Play Tribe and Civ with two players (the host and one partner): the partner's village is founded within a few seconds of
arriving.

## Phase 1 done (2026-11-11, feat/lineages-47): identity and the N-seat formats
* identity.{h,cpp}: one random 64-bit identity per Spore profile (SporeCoop\identity.bin next to EditorSaves.package);
  a guest sends it to the host after every join (MSG_IDENTITY 83, u64); `coop.identity`. Two PCs (TITAN host + QA
  guest + MSI): the host knew p1 and p2; after the MSI left and rejoined it was network player 1 instead of 2 and the
  host still mapped it to the MSI's identity.
* founders: kMaxSeats 8; Founder.player (identity); DUO2 / CIV2 (u8 seats, per seat species, faction, u64 player)
  written only for 3+ seats or a known identity (DUO1 / CIV1 byte for byte as before otherwise); a roster may grow
  (seats added, an identity learnt once) but never lose or change a seat; OFT2 for the outfit histories of 3+ seats
  (an OFT1 file still reads after seats were added). tests/win_tests.cpp test_founders_multi.
* Gameplay unchanged: tribe.cpp / civ.cpp still found and bind seats 0 and 1 only.

## Remaining phases (each its own session, tested with 3-4 games: TITAN QA instances + the MSI)
1. Tribe binding: seat_of_player (identity first, the creature match for a seat without one), player_of_seat,
   founder_village(seat); village_commander -> per village; host_command checks the sender's seat; record the identity
   of a seat on its first match.
2. Tribe founding: auto_founders_tick founds one village per unseated partner (one per tick, per-seat pending state,
   next free site); the BUG-9137 settle / host-creation guards per partner.
3. Tribe rules that count founders: h_NounData, pace_holds, missing_last_objective (6/7), six rivals (8),
   h_FactionFlag, village_allied_with_founder, h_TribePlayerOwned / h_MaxPopulation / h_BTTick, shared_hut_upgrade.
4. Tribe state on the wire: MSG_TRIBE_STATE food entries and the rescue trailer for N seats (a new tag), the guest's
   food line, g_founder_rescues / g_remote_rescues per seat, protect / rebuild / capture with an explicit seat,
   CMD_REBUILD / CMD_GIVE_UP with a seat, the outfit vote per seat, performances per performer seat.
5. Civilization: founder_owner per seat, founder_tick (a capital per partner), purchases / cursor / offline control /
   campaign_tick / hud progress for every partner nation (with the Civ items BUG-9054 partner nation AI and budget, BUG-9056
   shared progression, BUG-9073 turret purchases).
6. Space: begin_personal_launch takes the joining player's seat; MSG_SPACE_LAUNCH for N seats.
7. More than four: world.cpp's lobby check `p.id >= seats`, FST7 seats (1..4), the campaign seat UI; kMaxSeats 8.

## Design direction (owner, 2026-11-11)
* Partners' villages and cities are ADDED next to the native ones (one per player), never replacing a native tribe.
* They must come from the game's own creation and upgrade logic, per player, or co-op loses progression: the nest
  becoming the village at the Tribe start, the tribe becoming the city, upgrades on the player's own village. Today the
  founders spawn a fresh native NPC tribe (SpawnNpcTribe: 3 members, 15 food) 100-180 m from the host's village and
  take over a native "next city" (cCivExpansion::SpawnNextCity) in Civ; the game's progression sees only the host's
  tribe / nation and the mod copies progress over (shared hut upgrades, merged tech, BUG-9056 open).
* First research step: how the native Creature -> Tribe and Tribe -> Civ transitions create the player's tribe / first
  city (from the nest, from the tribe), and whether each partner's own game can treat its own village as the native
  player tribe (cGameNounManager GetPlayerTribe / SetPlayerTribe, one per game) while the host stays the world's
  authority. That per-player perspective also suits a later open-world fork (independent players and packs).

## Decisions (2026-11-12)
The owner's confirmed decisions for 3-4 players with own nests, and the proposed foundation phase, are in
docs/ROADMAP.md (the overview kept up to date); this ticket keeps the code-level details.

## Phase 5 (Civilization) in code (2026-11-12, feat/lineages-47; not yet run in a game)
Switch `civ.multi_founders` (on; off = the two nations of before, byte for byte the old code path). Details and the
rules: docs/re/civ.md#civ-multi-founders; pure rules mods/coop/civ_seats.h, unit test tests/civ_seats_tests.cpp
(fails when the two-founder rule ignores the identity or the number of partners).
* founder_owner per seat: host `party::founder_player_of` with the species match (nation species or the seat's outfit
  history) and the two-founder rule (one partner, a two-seat roster without identity: that partner, as before); a
  guest its own seat (`civ_seats::own_seat`: identity in its copy of the roster, species, the two-founder rule).
  Feeds h_CityOwned, h_VehicleOwned, host_assign_cities, the vehicle owners of host_tick and the purchase check.
* A seat without identity learns it from the partner who alone plays its species (roster written once, CIV2).
* founder_tick: one capital per Tribe roster partner seat, one at a time in seat order (per-seat spawned bits), only
  while every city is the host's or a seated founder's; the new nation is the only blank unknown one; the roster is
  written after each, the world saved or shared after the last. Recovery recreates missing saved capitals one at a time.
* Per seat: the guest's purchase dialogue and terrain cursor, CMD_CITY_PURCHASE (the sender's seat), the host's
  offline selection (every absent seat), campaign_tick (tech, highest count, unlocks over every founder nation),
  h_HudProgress (every partner nation's cities), the founding AI hold, `coop.civ bind_guest <nation> [player]`.
* No wire format change. The guest learns its nation from the roster file in the shared save, as before.

Still two nations or open:
* A Civ seat needs a Tribe roster seat (founders::can_record): with today's Tribe code (seats 0 and 1 only) a third
  player gets no capital until phase 3 founds their village, or a late-joiner rule (phase 4) lets founders.cpp
  record a Civ seat without a tribal one.
* Founding is refused once an NPC nation exists ("not reserved before other nations appeared"): a partner seat
  added to an advanced Civ save needs phase 4's catch-up capital.
* The capital takes the next free native site, not the one nearest the partner's former village: the site record's
  position field is unknown and a guest replays "the next site"; `coop.civ sites` dumps the records and the
  founding logs the capital's distance to the former village (docs/re/civ.md#civ-multi-founders).
* `.duo-command` (CMD1) binds the home and seat 1 factions only; its flag covers every absent seat. A per-seat binding
  needs a CMD2 format in founders.cpp.
* party seats (`.duoparty`, join order) and founder roster seats (Tribe order) are separate lists; civ uses the
  founder roster's seats only.
* Not touched: BUG-9054 (shared budget), BUG-9056 (history, Space readiness), BUG-9073 (turret planner) beyond the
  per-nation generalisation above.

To verify in a live three-player Civilization game (host + 2 partners, a Tribe save whose roster has three seats):
1. Host log: `civ automatic founders: Native partner capital founding requested.`, then seat 1 bound, then
   `Native capital founding requested for partner seat 2.` and `Partner seat 2 now has a native Civilization
   capital.`; `seat N capital ... from its former village` lines; one world share after the last capital.
2. `coop.civ nations` on all three games: three founder nations with the three species; `coop.civ` status
   `founders:` line: host seats 1 and 2 with their players, each guest `local_seat` 1 or 2 (its own).
3. Each partner selects and moves only its own nation's vehicles, opens only its own cities' menus and buys there
   (city menu purchase debits the shared treasury); the other partner's vehicles cannot be selected.
4. A city purchase (economic) by partner 2 is made by nation 2; a capture by partner 2 raises the home campaign
   counter (`coop.civ campaign`), tech and unlocks match on all three nations, the HUD bar counts all cities.
5. Partner 2 leaves: its nation's vehicles have no owner (0xFE) unless offline control is on (then the host selects
   them, also while partner 1 stays); it rejoins under another network id: it commands nation 2 again (identity).
6. Save, quit, reload: the capitals keep their species, nobody's nation is founded again, owners return.
7. With `coop.fix civ.multi_founders off` and two players: the old behaviour (lab duo_civ_* tests).

## Roadmap phase 3, Tribe parts (2026-11-12, feat/lineages-47): built, not yet run in a game
Remaining phases 1-4 above (Tribe binding, founding, rules, state on the wire) in tribe.cpp behind `tribe.multi_founders`
(on; off = the two-founder co-op exactly as before). Details: docs/re/tribe.md#tribe-multi-founders. Pure parts
(counts, the TRRS rescue trailer, the command seat word, the founding hold for older seats, the HUD food line) in
tribe_seats.h with tests/tribe_seats_tests.cpp (host unit test, in `make test`).
* Seat binding through party::founder_seat_of / founder_player_of (identity, else the creature match); a seat without
  identity learns its partner's identity once (founders::record, DUO1 becomes DUO2); `coop.tribe bind_founder`.
* Founding: one village per unseated partner, one a tick, the partner's own Creature nest as the site when usable,
  else the next free site; BUG-9137 guards per partner; one checkpoint after the last.
* Orders, ownership, food, purchases, performances, outfit votes, rescue / capture / rebuild per seat; every rule that
  counted two founders counts the founder seats (h_NounData, pace_holds, missing_last_objective 4 + seats, backfill
  5 + seats, six-rival trial 6 + seats, h_FactionFlag, village_allied_with_founder, shared_hut_upgrade, rewind hold).
* Wire: two seats byte for byte as before; 3+ seats: a food entry per founder village, rescue trailer TRRS
  (n x {u16 member, u16 tenths}, u8 n, u32 tag); CMD_REBUILD / CMD_GIVE_UP: seat + 1 in the action word (0: not given).
* Not changed (fixtures with exactly one guest): `coop.tribe register_guest / found_guest / refound_guest`.

To verify live (host + two partners, three games; then once with the switch off):
1. Fresh Creature -> Tribe with three players (each with an own creature and nest): both partners get a village
   (log `tribe automatic founders: seat 1 / seat 2 ...`, `at their own nest` or `at the next free site`), one
   checkpoint reload; `coop.tribe` on the host: `seats=3`, both partner seats with an identity and a commander.
2. Each partner selects and orders only their own village's members (walk, gather, raid, equip, build, baby, perform);
   the other partner's members are refused (`coop.tribe` rejected count on the host).
3. Food line on each guest (`Host X   You Y`) shows its own village; the host's line lists both partners.
4. Down the last villager of partner 2's village: protected; partner 1's or the host's villager revives it; a guest's
   hold-G gives up only its own; capture and `coop.tribe rebuild` from another founder rebuild that seat.
5. Tribal outfit vote (all three edit): each village gets its own partner's outfit, one checkpoint.
6. Rejoin of a partner (another network id): the same seat by identity; a third partner joining later gets the next
   seat; with an older save (seat 1 without identity) the seat-1 partner is recognised by creature and bound.
7. Campaign: a rival appears with three founders, goals count founders + NPCs (`coop.tribe npc_budget`), hut upgrades
   reach both partner villages.

## Phase 3 live test (2026-11-12, feat/lineages-47)
Three games on two PCs (QA host + QA partner on TITAN at 5 FPS, the MSI as the third), a fresh copy of the owner's
Jiff Peanutbutter save (seat 1 founded with the host's creature, BUG-9137). Passed: checks 1 (on an existing Tribe
save, not a fresh Creature start), 2 (orders of each partner reach only its own village: 5 done, 0 rejected), 3 (food
lines), 6 (a rejoin keeps the seat; a later partner gets the next seat; the older seat is claimed and bound). Fixed on
the way: the free-site search found no spot for seat 2 (rings around the host's village only); it now also searches
around the other founder villages (docs/re/tribe.md#tribe-multi-founders). Not yet live: checks 4, 5, 7, 8 and a fresh
Creature -> Tribe start (needs the host's clicks), and all of phase 5 (Civilization).

## Phase 4, Tribe parts (2026-11-12, phase4-tribe): built, not yet run in a game
Owner decisions 4, 5 and 9 on the Tribe side. Details: docs/re/tribe.md#tribe-late-joiners; pure rules in
mods/coop/tribe_late.h with tests/tribe_late_tests.cpp (host unit test, in `make test`).
* Decision 4, first half (a returning player keeps their species): already works, no code. A guest's creature is its
  explicit choice, else its campaign creature (choices.ini [landfall], host name + planet), else the own creation
  recorded for the planet (worlds.ini), else the host's species; share_own sends it, the host founds or rebinds.
* Decision 4, second half, `tribe.late_prompt` (on): a partner who plays one of the host's creatures gets one notice
  both sides can read ("X plays one of the host's creatures, so they have no village of their own. They can choose
  their own creature in the Co-op window (F9)."); the host's Co-op window status line names them; the partner's Co-op
  window shows the creature row in Tribe: "Choose your own creature for this game: <name> < >" and a status line. A
  pick reaches the host's founding without a rejoin (about 10-17 s: share 2 s, settle 10 s, founding tick 5 s).
  A guest who joins straight into Tribe on the host's species usually sends no creature at all (no avatar in Tribe);
  that partner waited without a word before and is now told too ("X has no creature of their own here, ..."), once
  their state shows them in the host's world.
* `tribe.late_create_editor` (OFF): "Create New Creature" opens the creature editor (0x465C50BA, no creation key) from
  Tribe; the creation saved there becomes the partner's creature. Not verified (editor with an empty key, the return
  to Tribe, a host checkpoint during it): needs a live test first.
* BUG-9138 (`tribe.founding_skip`, on), found on the way: a partner on a founder's species stopped the founding of every
  partner after them in join order, silently for the host's species. They now wait with the reason above.
* Decision 5, `tribe.late_catch_up` (on): a partner village founded while the host's village is past the start gets,
  once and before the founding's checkpoint, the host village's hut level (Community_SetLevel +
  Tribe_UpdateHutModel), member count by age (the game's SpawnMember under the shared population limit) and tool racks
  (the planner's CreateTool into free layout slots, purchase history set). The host's village is only read; villages
  of older saves are never touched (decision 9). shared_hut_upgrade already reached a later village (every partner
  seat, every second), one share save after the founding; the level now comes with the founding's checkpoint.
* Test commands: `coop.tribe late` (the late-joiner state on this game), `coop.tribe shapes` (every village of this
  game: level, adults, babies, rack mask), `coop.tribe hut_level <0..4>` (host, test setup), a `tribe late:` line in
  `coop.tribe`.

To verify live (three games: host, partner A with a village, partner B joining late):
1. B joins with `coop.creations choose creature none` (plays the host's species): after about 15 s one notice
   appears on all three games (B sends no creature: "B has no creature of their own here ..."; B sends one of the
   host's creations: "B plays one of the host's creatures ..."); the host's F9 status line "B has no village yet:
   ..."; `coop.tribe late` on the host names B (`waiting=`); B's F9 lobby shows "Choose your own creature for this
   game: none yet" with < > and the status line; `coop.tribe late` on B: `prompt=1 has_village=0 plays_own=0`; no
   village for B.
2. B clicks ">" (or `coop.creations next creature`) onto an own creature: within about 20 s the host logs
   `tribe automatic founders: village for player B ...` and one checkpoint reloads all games; afterwards B's row is
   gone (`prompt=0`) and B selects and orders only their own members.
3. Catch-up: before B's founding, grow the host's village (`coop.tribe spawn 4`, `coop.tribe build 1`,
   `coop.tribe build 5`, `coop.tribe hut_level 1`), keep `coop.tribe shapes` of the host's village. After B's
   founding: host log `tribe late catch-up: seat N ...: hut level 1, members +A of A ..., racks 1 5`;
   `coop.tribe shapes` on the host and on both guests (their own loaded worlds after the checkpoint): B's village has
   the host's level, the host's adults and babies, racks mask with bits 1 and 5; the host's village line unchanged.
   With `coop.fix tribe.late_catch_up off` (FAIL control): B's village has 3 adults, its own level, no racks.
4. BUG-9138: B joins first playing the host's species, C (or A on a fresh save) joins after with their own creature:
   C gets a village while B waits with the notice; `coop.fix tribe.founding_skip off`: C gets none.
5. B leaves and rejoins: same seat, no second village, no second `tribe late catch-up` line.
6. The owner's Jiff copy (older partner village): seat 1's `coop.tribe shapes` line unchanged by the session's
   catch-up (decision 9).
7. Only on the MSI (it needs clicks): `coop.fix tribe.late_create_editor on` on B, F9 "Create New Creature": does
   the editor open with a blank creature, does Save (3+ parts, a name) put a new creation into My Creations, does the
   exit come back to Tribe (`creations: back from the editor, own creature ...` in B's log), and does the host then
   found B's village. Watch the host: a checkpoint or share while B is in the editor.

## Phase 4, Civilization parts (2026-11-12, phase4-civ): built, not yet run in a game
Roadmap phase 4 (owner decisions 4, 5, 9) on the Civilization side. Switch `civ.late_capital` (on; needs
`civ.multi_founders`; off: as phase 5 above). Details: docs/re/civ.md#civ-late-capital. Pure rules in civ_seats.h
(`late_status`, `late_capital`, `site_state`, `new_city_since`, `catch_up_buildings`, `catch_up_turrets`) and
founders.h (`nations_descend`), host unit test tests/civ_seats_tests.cpp: each rule replaced by the behaviour before
it fails the test (4 of 4 checked); tests/win_tests.cpp checks the files (3 checks fail on the old founders.cpp).
Commits ae6f76c4 (rules, can_record), 405644fb (win_tests, BUG-9139), 5aaeb3eb (civ.cpp).
Two of the "Still two nations or open" items of phase 5 are addressed:
* A Civ seat without a Tribe roster seat: founders::can_record accepts a seat past the Tribe roster when it has its
  player's identity and a species that is no tribal founder's look; tribal seats keep the look rule; two founders stay
  CIV1 byte for byte.
* Founding once NPC nations exist: the late capital founds the next seat (a late joiner, or the Tribe roster's next
  seat) at the next free native site and tells the new city by its object (NPC nations can be blank too); the host's
  own planet tick founds nothing else meanwhile; the other nations' AI waits as before.
* Eligible: a connected party partner (identity, party seat) playing a creature of its own (not the host's), loaded
  here, of a species no nation or tribal founder has, with no founder seat. One at a time, in seat order (Tribe
  roster seats first, then party seat order), up to four seats. No own creature: no capital, a log line and a notice
  (the create-a-creature prompt is the Tribe side's). A game with no roster at all needs saved campaign rules
  (`.duo-seats`); a stage-chooser game keeps one shared nation.
* Catch-up to the host's capital (not its empire): its houses, factories and entertainment buildings the new capital
  lacks (the planner sequence, free, the host's slot index where free) and its turrets (City_AddTurret); then one
  save or world share; the roster is written at the bind (CIV2).
Still open:
* The "level" of decision 5: no city level is known in Civilization; the nation's tech and unlocks already follow the
  host's (campaign_tick). `coop.civ late` prints each founder capital's +0x38 word (a cCommunity's level for tribes)
  for a check in a game.
* A site near the host's capital: the site record's position field is unknown; the next free site is used and the
  bind logs the distance.
* A late capital whose city did not reach the save before a crash is not founded again once NPC nations exist.
* The Tribe side of phase 4 (the create-a-creature prompt, the village catch-up) is the other helper's.

To verify in a live Civilization game (no lab run here; the main session):
1. Late joiner, NPC nations present: a Civ save with founder nations (seats 0 and 1, or 0..2) and at least one NPC
   nation; a further partner joins with a creature of its own (a party seat with identity). Host log:
   `civ late capital: founding requested for seat N (<name>) with K cities`, then `seat N (<name>) capital <pol> at
   (x y z), D m from the host's capital; roster written`, then `caught up with the host's capital: a of b buildings,
   c of d turrets`, one world share. `coop.civ late` on the host before and after (status per partner,
   `next_seat`, each founder capital's buildings / turrets / word38).
2. On every game (`coop.civ nations`, `coop.civ citizens` as the camera passes the capital): the new nation has the
   joiner's species; its capital has as many houses, factories and entertainment buildings as the host's capital
   (free slots allowing) and as many turrets; no NPC or host city changed owner, species or buildings.
3. The joiner (after the share) commands only its own nation: selects and moves its vehicles, opens its own capital's
   menu and buys there (shared treasury); `coop.civ` status `founders:` shows seat N with its identity and player;
   the guest's `local_seat` is N. The other partners cannot select its vehicles.
4. A partner who plays the host's species (no own creature): no capital, the notice "<name> plays the host's species:
   no capital of their own until they create a creature." on every game, `coop.civ late` says so.
5. A Tribe roster seat whose capital was not founded before NPC nations appeared (a 3-seat Tribe roster, a 2-nation
   Civ save with NPC nations): the log has the late founding for that seat instead of "not reserved before other
   nations appeared", its species is the seat's current look.
6. Save, quit, reload: the late capital keeps its species and seat, nothing is founded again; the joiner rejoins
   under another network id and commands it again (identity).
7. With `coop.fix civ.late_capital off`: no late capital (the phase 5 behaviour); `late_deferred` stays 0.
8. Evidence for the open items: `coop.civ late` word38 of the host's grown capital and a fresh one (a city level?),
   the logged distance to the host's capital, `coop.civ sites` before and after the founding (the site record's
   position field).

## Phase 4 live test, Tribe (2026-11-12, feat/lineages-47)
Two PCs, the Jiff test save (3 reserved founders): a joiner on the host's creature was told why it had no village and
got the Co-op window prompt; its pick founded seat 3 with the catch-up (hut level 1, +4 members, racks 1 and 5); the
MSI's late village (seat 2) the same; all games agree on four founder villages; the old seat 1 kept its shape. Not
live yet: BUG-9138's case (a waiting partner before a ready one), the editor button, the Civ late capital. Found: a
fourth player is refused "session is full" when the campaign reserved 3 founders (owner decision asked, docs/ROADMAP.md).

## Seats added mid-stage (2026-11-12, feat/lineages-47; owner decision 13, "2A")
The reserved founders (.duo-seats) could change from the next stage only, so a fourth player was refused on a
campaign that reserved 3. Now `founders::raise_now` / `raise_seats` raise the running stage's count at once (Cell to
Civ; a stage reopened after its next one began raises its own `previous_stage_seats` entry; a queued change to as many
seats or fewer is dropped, one to more stays). `world::raise_seats_now` re-applies the rules (session seat limit, the
rules sent to the guests, the cached rules dropped), so every difficulty extra reads the new count at once.
`world::set_seats` is the Co-op window's button, renamed "Set Seats": more seats now, fewer from the next stage (Space:
from the next load, as before). Console: `coop.seats N now`; `coop.seats N` queues as before. Lost on a raise: only the
saved fractions of conversion and Cell credit (both keyed by the old percentages; under one point each).

Live (Jiff test copy, TITAN host + Late + the MSI, then a fourth TITAN game): at 3 seats the fourth join was
"rejected: session is full"; `coop.seats 4 now` answered "Reserved founders: 4, ... (added now)"; `coop.rules` on the
host and both guests read seats=4, enemy_hp 145%, takeover 115% at once (130% / 110% before); the fourth player joined
(4/4), read the same rules, and was seat 1's commander again (its identity). Not tested: the button itself (same
call path: set_seats -> raise_seats_now), Cell, Creature and Civ.

Seen on the way: the three villages left alone for 4 hours (5 FPS) were down to their chief (7 -> 1); the co-op epic
(tribe.cpp, "an epic creature comes!") visits the host's village every 10 minutes and nobody defended. This explains
the MSI village's loss of 7 -> 5 noted in docs/re/tribe.md#tribe-late-joiners; normal play, not a bug.

## Phase 4 live test, Civ (2026-11-12, feat/lineages-47)
Four games (TITAN host + 2 TITAN guests + the MSI) on the Abber Civ campaign save: three late capitals, one per
partner, each caught up with the host capital (4 of 4 buildings, 2 of 2 turrets), each commanded by its player, kept
after a rejoin; the same capitals, owners and money on every game; 0 faults. Found and fixed: a late joiner without a
creature of their own got only a chat notice in Civ (the Co-op window had no creature choice there): civ::late_prompt
now shows the Tribe prompt in Civ, and the row's arrows choose the creature; a real click on the MSI then founded its
capital within 40 s. Details: docs/re/civ.md#civ-late-capital. Not live yet: the Tribe -> Civ graduation with 3-4
founder villages (phase 5).

## Phase 5 live test, Tribe -> Civ graduation (2026-11-12, feat/lineages-47)
Four games, a fresh copy of the owner's Jiff save: four founder villages (seat 1 bound with `coop.tribe bind_founder 1
2`), then four wins by fixture and one native win (docs/06-testing.md#testing-two-pc-msi), the native "Your totem is
complete" caption, History, the ready vote (`coop.vote go`), the City Hall and vehicle editors, Civ: one capital per
founder seat with its own species and owner on every game, 0 faults. Details: docs/re/civ.md#civ-four-founders-live.
