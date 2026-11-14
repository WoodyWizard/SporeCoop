# SporeCoop Lineages roadmap

The owner's decisions and the plan that follows from them. Updated whenever a decision is made or a phase moves.
Details of each piece of work live in its ticket (docs/tickets/); this file is the overview. Dates are when a
decision was made.

## Long-term goal (not committed)

An open-world MMO fork of the co-op mod (owner, 2026-11-11): every player plays on their own planet, a planet holds
roughly 10-20 players, players can spawn on almost any habitable planet; fully independent players and packs; a
party system for real players; difficulty per party; repopulation of extinct species after a while.

Design rules that follow from it, for the co-op work now:

* A player is a persistent identity, never a network slot (identity.cpp, BUG-9135).
* Every player owns their own things: species, nest, pack, village, city. The host is the shared world's authority,
  not "the" player.
* Things are created and upgraded through the game's own logic, per player (the nest becomes the village, the village
  becomes the city), so each player's own progression carries over (owner, 2026-11-11).
* No fixed player counts in code paths; data structures hold more than the menus allow.
* Sync what is near (interest management); per-planet sessions later.

## MMO questions to discuss (owner, 2026-11-12: "soon", not blocking the co-op work)

* Players outside your party: their species is another species from your game's point of view, so you can impress
  it or hunt its nest family to extinction by the game's own rules (each player's game keeps its own species
  relationship table, which fits per-player relations). To decide: what happens to a player whose species goes
  extinct (respawn, the repopulation timer); whether players can attack each other's creatures directly (PvP); how
  DNA farming of other players is prevented.
* Co-op code keeps this open: the protection of decision 7 is "same party", never "every seat"; the game's own
  impress / hunt logic stays for every species outside the party.
* Space (briefing 2026-11-12, research only): today every player flies in their own copy of the host's galaxy, linked
  by star and planet ids; one empire for the whole session (shared money and diplomacy, spacecoop / shared.cpp), an own
  ship and captain per founder, enemies shared only in the host's star system (squadron, hard-wired to player 0), a
  guest's new colonies shown to the others as an allied "<name>'s empire" and given back on a rejoin from the host's
  memory only (space_allies, space_map), the Galactic Core waiting for the whole session, difficulty per slot. For 3-4
  players: the Civ -> Space hand-off carries two founders only (world.cpp, civ.cpp), and Space does not use the party /
  identity layer yet (keyed by network id or name). Carries over to the MMO: one game per area, id-linked copies,
  allied empires of other players, Rivals mode's own budgets. Would be redone: host = player 0 in charge, waiting for the
  whole session, keying by name, colonies in memory only, per-slot difficulty. Decisions asked 2026-11-12: what a
  player owns in Space (recommended: an own empire each, a party as a standing alliance), phase 6 scope (recommended:
  keep one shared empire for co-op now but move Space onto the party / identity layer, launch every seat, colonies saved
  per identity in a side file), who runs enemies in a system (recommended: whoever of the party is there, handed over
  when they leave), where Space progress is saved (recommended: a side file per identity), difficulty (recommended: per
  party), the Galactic Core (recommended: your own party only).
* Space, rethought (owner 2026-11-12: "Consider the idea that multiple people will be progressing out of the planet,
  and especially in MMO, non partied members may progress out the planet too."). The native end of Civ is "own the
  whole planet", which with several parties on one planet means beating the other players first. Asked 2026-11-12:
  what ends Civ for a party (recommended: the party's own progress target, the other parties keep playing Civ);
  what happens to a party's cities when it leaves (recommended: they stay as its homeworld, nobody can capture them);
  who launches (recommended: the whole party together, each player in their own ship; a later joiner launches from
  the homeworld); what Space players may do to a planet where others still play Civ (recommended: visit and trade
  only, no raids or abductions of players' cities).
* These Space questions are decisions 1X-2C and 2H-2K in the decision log (Open decisions).

## Confirmed decisions: 3-4 player co-op with own nests (owner, 2026-11-12)

These are 1A-1K, 1N-1Q in the decision log (Open decisions); the numbers below are kept.

| # | Decision |
|---|----------|
| 1 | Lineages 54 (the 2026-11-11 fixes) is published first, as its own release, before this work starts. |
| 2 | Each player has their own nest in Creature, about 40-80 m from the party leader's (the host's). A party that came from Cell together lands together. |
| 3 | When the host's species migrates, every nest moves with it, each to its own spot near the host's new nest (the party moves together). |
| 4 | A player who joins with a species from the previous stage keeps it automatically and evolves along with the host. A late joiner without one is asked to create a creature, which is their species for the rest of the game. |
| 5 | A late joiner's home is caught up to the host's home only. Tribe: the same members, hut upgrades and tools as the host's village. Civ: one capital with the same buildings and level as the host's capital (money is already shared). Not the host's whole empire. |
| 6 | A late joiner in Creature gets their own nest of their own species next to the party (the game's own herd-and-nest creation, cGameNounManager::CreateHerd; to verify first; fallback: share a nest with their own creature, as today). At the Tribe start their village comes from their own nest. |
| 7 | Party members' species are best friends with each other: nobody in the party can hunt another member's species or nest, also while that member is offline. Species outside the party can be hunted. |
| 8 | An offline player's nest is run by the AI and can die out naturally; when the player returns, an extinct nest is rebuilt. |
| 9 | Saves that already have a partner village (founded before this work) keep it as it is; only new stage starts use the nest-based creation. |
| 10 | The player cap stays at 4 in the menus; difficulty stays per seat for now (it must become per party for the MMO goal: per-seat scaling does not work at 10-20 players). Data structures hold 8 seats (founders::kMaxSeats). |
| 11 | Tests: up to 3 test games on the owner's PC (5 FPS, small windows unless a test needs a size, e.g. Adventures; console only, never the owner's mouse) plus the MSI over Tailscale; real clicks only on the MSI. |
| 12 | The live Civ test may click in the test host's game window on the owner's PC, once, while the owner is away (owner, 2026-11-12: "1A"). |
| 14 | A late joiner without a creature of their own (owner, 2026-11-12: "1B ... make it clean and seamless; if not possible, 1A"): Tribe: their village is founded at once with a stand-in that looks like the host's creature (a re-keyed copy, so every per-seat rule holds), caught up, and switches to their own creature in place (the outfit path, no villager replaced) when they make one. Civ (no clean species change known): 1A: the creature editor first; meanwhile a blurred view (the host's, an AI's or their home's camera) with a progress ring and short steps ("Catching up to Host...", "Matching tools...", "Buildings are rising...", "Final touches..."). Research: docs/re/tribe.md#tribe-late-joiners (stand-in notes to add with the build). |
| 15 | The owner calls the reserved seats "Player Slots" in every text; the lobby has an Apply button; no Send World button (the world goes out by itself). |
| 13 | The host can raise the reserved seats in the middle of a stage (the Co-op window's Set Seats, `coop.seats N now`); the difficulty of the extra seats applies at once. Fewer seats still apply from the next stage; Space keeps "from the next Space load" (owner, 2026-11-12: "2A"). |

## Health check (2026-11-12)

Measured, not guessed:

* Runtime cost is small: the co-op tick averages 0.011 ms per frame (max 2.7 ms), the effects hooks 0.0014 % of the
  main thread (owner's 72-minute session, 3 players).
* Process is solid: 741 tickets, 14,700 lines of reverse-engineering notes, 17 host unit-test suites (all passing),
  483 lab tests, every fix behind a switch so a test can prove it.
* Structure is the weak point, and it matters for the goal:
  * Four files of 5,900-7,500 lines (tribe, coop, civ, cellsync) mix syncing, game rules, UI and test commands.
  * File-wide state (210 globals in tribe.cpp, 161 in civ.cpp, 162 in cellsync.cpp) assumes "the host and one
    partner": 80 `guest()` uses and 16 exact-two-player checks.
  * Every stage keeps its own idea of who owns what (founders in tribe and civ, guest_pack, the nest follow); there is
    no shared ownership layer.
  * 506 switches: each keeps the old code path forever; nothing retires them.
  * The Windows build does not track header changes (a header edit needs FORCE=1; a partial build crashed both test
    guests on 2026-11-11).
  * The Windows and two-PC test scripts of 2026-11-11 live outside the repository (not repeatable by others).

## Plan

Phase 0 (foundation) comes before phases 2-5 (owner, 2026-11-12: "1A").

| Phase | Work | Status |
|---|---|---|
| 0a | Build tracks header dependencies (-MMD depfiles): no more stale objects. | done 2026-11-12 |
| 0b | One ownership layer (`party`): identity -> seat -> owned things (species, nest, pack, village, city) for every stage; the 4-player work goes through it instead of patching the `guest()` spots in place. | done 2026-11-12 (party.cpp, .duoparty) |
| 0c | Split tribe.cpp and civ.cpp along the seams that work touches (founding, ownership and orders, campaign rules); no big-bang rewrite. | decided |
| 0d | The two-PC test harness (MSI tunnel, launch, checks) into tools/ as repeatable tests. | done 2026-11-12 (tools/qa_win) |
| 0e | Switch retirement: a fix stable for 3+ releases loses its switch and old path, in small tested batches. | batch 1 done 2026-11-12 (5 of 506: tribe.guest_attacks, effects.other_stages, core.away_goal, team.rescue_reset, civ.citizen_far; only switches no test or tool flips) |
| 1 | Player identity and the 8-seat founder save formats (DUO2 / CIV2 / OFT2). | done 2026-11-11 (BUG-9135) |
| 2 | Creature: own nest per player (decisions 2, 3, 6, 7, 8), via the game's herd-and-nest creation; respawn and hatch at the own nest; migration together. | done 2026-11-12, two-PC tested (docs/re/creature.md#creature-own-nests) |
| 3 | Tribe: each player's village from their own nest through the game's Tribe-start logic; 3-4 founders in every rule that counts founders (objectives, rivals, alliances, hut upgrades); orders, food, rescue and outfits per seat. | built 2026-11-12 (`tribe.multi_founders`); live-tested with 3 players on two PCs on a copy of the owner's Jiff save: the old broken partner village repaired and bound, a third village founded, orders and food per seat, a rejoin keeps its seat. Not yet live: a fresh Creature -> Tribe start (needs the host's clicks), rescue / give-up, the outfit vote. (docs/re/tribe.md#tribe-multi-founders) |
| 4 | Joining late (decisions 4, 5): detect a species, else the create-a-creature prompt; catch up to the host's home. | built 2026-11-12. Tribe live-tested with four founder villages on two PCs: a joiner on the host's creature is told why and gets the "choose your own creature" row in the Co-op window; their pick founds their village with no rejoin, caught up to the host's (hut level, members, racks); all games agree. Civ live-tested 2026-11-12 with four games: three late capitals (one per partner) caught up with the host capital; a joiner without a creature now gets the same prompt in Civ (fixed after the test showed only a chat notice). Tribe stand-in villages (decision 14, BUG-9150) built and live-tested 2026-11-12: a joiner without a creature gets a village at once that wears the host's creature, and their own creature replaces it in place. Not yet live: the "Create New Creature" editor button (off), BUG-9138's two-waiting-partners case, the Civ loading screen (decision 14, Civ part). (docs/re/tribe.md#tribe-late-joiners, docs/re/civ.md#civ-late-capital) |
| 5 | Civ: each player's city from their own tribe; 3-4 nations in the campaign rules (with BUG-9054, BUG-9056, BUG-9073). | built and live-tested 2026-11-12: a native Tribe -> Civ graduation with four founder villages (TITAN host + 2 TITAN guests + the MSI) founded one capital per founder seat, each with its own species and commanded by its own player, on every game; 0 faults. (docs/re/civ.md#civ-four-founders-live) |
| 6 | Space: the personal launch per seat. | step 1 live 2026-11-13: four founders launched from a Space-ready Civ save (fixture profile DuoQAJiffSpaceReady), each with an own ship and captain, 0 faults; a rejoiner's launch fixed (BUG-9187); each guest's own colonies kept per identity in a side file and back after a host restart (BUG-9189). Open: the rest of steps 2-3 below, a guest's blank homeworld card (BUG-9188). (docs/re/space.md#space-four-founder-launch) Enemies per system (1Z, 3D A): built 2026-11-14 (BUG-9197). Step 2, Space keyed by identity: built 2026-11-14 (BUG-9198: rivalry, budgets, ally mirrors; captains and rosters are live state). |
| 1b | Stable species keys per identity (a partner's creature keeps its key across rejoins under another network id). Review 2026-11-13 (decision 2S): widen to one key per player creature on every game (a guest's own key and the host's key of it are two species there: the cause of BUG-9169, BUG-9173, BUG-9175's herd, BUG-9176); recommended before more Creature features. | step 1 done (BUG-9177, the player species); step 2 done 2026-11-13 (BUG-9180: one key per creation, from its content; live on two PCs) |

1b as built (2026-11-13, 2X A, BUG-9180): the key is a hash of the creation itself (no protocol change, no identity
needed: the bundle every game already gets), a guest plays it on its own game. The plan below said "from the persistent
identity"; the content alone does the same with less. Still two keys: the host's own creation on guests (0x7D, its
world's species), cells and UFOs.

1b design (2026-11-13, decision 2V A; the choice in 2X). Today a player's creation has two keys: its own My Creations
key on its own game (2920408F) and 0x7C000000 | fnv(owner's network id, content hash) on every other game
(creations.cpp shared_instance), so the key also changes with the network id. Two keys made: the host's copies of a
guest's family another species on the guest (BUG-9169 converts them every second), a herd of the wrong species, "your
species is extinct" (BUG-9176), a new nest each session (BUG-9173). Plan A: the owner computes its creation's shared key
from its persistent identity (identity.h, not the network id) and the content hash, sends it in the bundle, and plays
that key itself (its records registered under it, as every other game does); older partners keep today's key. Then
removed: npc own_family_tick (BUG-9169), the nest note (BUG-9173, kept one release to migrate old saves), BUG-9176.
Kept (not about keys): the player species (BUG-9177), the mate offer (BUG-9174: copies' AI never answers), the direct
mating (BUG-9175). Tests: a guest rejoining under another network id keeps its key and nest; its family on its game is
its species with no conversion; an old save's seat nest is taken over once; the host's save loads on a guest.

| 7 | Adventure creatures simulated by the host (BUG-9114). | built 2026-11-14, on by default: the host's enemies strike partners (BUG-9200), the guest's leftovers go and the host's kills die there (BUG-9201), the guest's hits count; live PASS/FAIL on TITAN; not yet tested on two PCs |

The phase details (code spots, formats, messages) are in docs/tickets/BUG-9135-no-own-villages-three-players.md.

Phase 6 survey (2026-11-13, overnight; nothing built yet). With 1X A ("A party will always progress together and
share") co-op keeps one shared empire, so 2N's protected home (a planet several parties leave) matters for the MMO, not
for one party: a guest's game loads the host's save, so the party's homeworld is everyone's own home there (the game
calls it the player's), and a partner's colonies show as an allied "<name>'s empire" (docs/re/space.md section 15). The
two-founder spots to change, in order:
1. The Civ -> Space hand-off (2J, every seat launches): world.cpp launch_archive keys the archive by the pair
   (home + guest() species and factions), the launch offer (MSG_SPACE_LAUNCH, world.cpp send of the Offer) writes and
   reads exactly two founders, creations::needs_launch_checkpoint takes the pair; civ.cpp founder_space_ready and about
   15 more places test `g_founder_nations.guest().faction` (seat 1 only) as "a founder world".
2. (built 2026-11-14, BUG-9198, docs/re/space.md#space-identity-keys) Space keyed by identity and party (party.cpp) instead of network id or name: spacecoop.cpp captains, space_allies.cpp
   and space_map.cpp colonies (kept in the host's memory only today), space_rivalry.cpp and space_diplomacy.cpp.
3. Colonies and progress per identity in a side file (2A); enemies run by whoever of the party is in the system (1Z,
   squadron.cpp is hard-wired to player 0); difficulty per party (2B); the Galactic Core per party (2C).
Test fixture needed first: a Space-ready Civ world with 3-4 founder nations (the four-founder Abber saves are not Space
ready).

Phase 6 survey of the Space code (2026-11-13, decision 3C A): no Space file uses identity or party; everything is
memory only and keyed by network id (spacecoop.cpp, space_map.cpp rosters, space_contracts.cpp, space_finale.cpp,
space_trade.cpp), by name (space_allies.cpp mirrors, space_rivalry.cpp relations and budgets) or by "the host is
player 0" (squadron.cpp, space_ext.cpp transport). Done: the guests' own colonies per identity in a side file
(BUG-9189). Enemies per system (1Z) design, not built: squadron.cpp today simulates and sends the NPC fleet on the host
only, and a guest mirrors it only while it is where the host is (with_host); elsewhere each game runs its own natives,
so two guests in one system without the host see different enemies. Plan: a leader per place (the host if there, else
the lowest party seat there), the leader runs host_tick's snapshot; MSG_UFO_STATE gets the leader's id in front and the
host relays a guest leader's snapshot to the players at that place (guests talk through the host only); hits
(MSG_UFO_HIT) go to the host, which applies its own or forwards to the guest leader; mirrors keyed by (leader, hid);
a hand-over when the leader leaves (the next leader keeps the ships its game already has). Estimate: several hours
and a three-game test with two guests in a system without the host.

## Playthrough status, 2-4 players (2026-11-12, asked by the owner)

| Stage | Playable now | Needed for a full playthrough |
|---|---|---|
| Cell | together, revive ring, respawns, spectating while knocked out in the Creature-style downed box with < > (BUG-9171, 2026-11-13, live with the MSI) | - |
| Creature | own nest and pack per player, best-friend partners, nests migrate together, revive ring (60 s), debt bar, downed pose and spectating (BUG-9156), personal evolving: own editor through the mating call, the game's cocoon, personal DNA, migration together (BUG-9160..9163, 2026-11-13), a joiner's own editor first and its DNA kept across reloads (BUG-9164, BUG-9165), a partner's nest follows its evolution (BUG-9151), a real migration across PCs with every player moving into their own new nest (BUG-9163, BUG-9166..9168), a guest's mate offered with hearts and the heart cursor as the game's own (real-mouse hover on TITAN) and a returning player's old nest kept (BUG-9174, BUG-9173), a guest evolving through its own mate and a dead family back in seconds (BUG-9175, 2E; live on two PCs) | the mate's pink card seen on a guest by the owner; one species key per player creature (1b, step 1 done 2026-11-13, BUG-9177: the game's player species follows the creature played; BUG-9176 and the patches of BUG-9169 / 9173 / 9175 come from two keys) |
| Tribe | own village per player played with the game's own controls, hut menu and planner (BUG-9152, BUG-9153), camera starts at home, food share and gifts (BUG-9154), party wipe (BUG-9155), late join with a stand-in village (BUG-9150), revive ring, no native game over while the party plays on (BUG-9147, 2026-11-13) | right-clicks from a guest live 2026-11-13: walk, gather, eat, party (BUG-9185), attack and raid pass; hunt reaches the host (the animal found; refused only while the villager was down); the gift on a rival's hut was missing and is built (BUG-9186), not seen live; the host's own game over by another route (BUG-9184) |
| Tribe -> Civ | four founders graduate, one capital each (phase 5) | - |
| Civ | own capital and nation per player, late capitals caught up, the party wipe (BUG-9155), a late joiner's editor first and loading screen (BUG-9170) | the capital founded for a late joiner seen live (needs a Civ save with a free founder seat) |
| Space | the party shares one empire (today's co-op); four founders launch together, each with an own ship and captain (2026-11-13) | phase 6: own empires (1X), the shared protected homeworld (2N, checked first), launching together (2J), progress per player (2A), enemies run by whoever is there (1Z), difficulty per party (2B), the Galactic Core per party (2C) |
| Adventures | playable | adventure creatures run by the host (BUG-9114, phase 7) |
| Everywhere | 4 player slots, identities, rejoin keeps the seat | publish Lineages 54 (1A; nothing pushed yet), a rejoin loads the world twice |

Order proposed: finish Tribe, Creature evolving, the Civ wipe and loading screen, Space (phase 6), release.

## Decided (2026-11-12)

* Phases 2-5 are built on the phase 0 foundation, not by patching the two-player code in place (owner: "1A").
* Switch retirement: a fix stable for 3 or more releases loses its switch and its old code path, in small batches,
  each batch with the host unit tests and a two-PC smoke test (owner: "2A").

## Open decisions

Decision log (owner, 2026-11-12): every decision ever asked has a permanent ID in the order it was asked: 1A, 1B ...
1Z, then 2A ... 2Z, and so on. Answers read "2HA" (decision 2H, option A). Options: A what Claude recommends, B the
closest to the owner's ask (with its issues), C an alternative; one answer per ID, no sub-decisions.

| ID | Decision | Answer |
|---|---|---|
| 1A | Lineages 54 is published first, before the 3-4 player work (table above: 1) | A |
| 1B | Own nest per player in Creature, 40-80 m from the host's (2) | A |
| 1C | Nests migrate together (3) | A |
| 1D | A joiner keeps a species from the previous stage; a late joiner without one makes a creature (4) | B, with the owner's detection rule |
| 1E | A late joiner's home is caught up to the host's home only (5) | A |
| 1F | A late joiner in Creature gets their own nest (6) | A |
| 1G | Party species are best friends; other species can be hunted (7) | A |
| 1H | An offline nest is run by the AI, can die out, is rebuilt on return (8) | A |
| 1I | Old saves keep their partner villages (9) | A |
| 1J | Cap 4 in the menus, difficulty per seat for now, data for 8 seats (10) | A |
| 1K | Tests: up to 3 test games on the owner's PC at 5 FPS plus the MSI; real clicks on the MSI (11) | A |
| 1L | Phases 2-5 built on the phase 0 foundation | A |
| 1M | Switch retirement in small tested batches | A |
| 1N | The live Civ test may click in the test host's window (12) | A |
| 1O | The host can add player slots in the middle of a stage (13) | A |
| 1P | A late joiner without a creature: a stand-in village at once, else the editor first with a loading screen (14) | B in Tribe (built, BUG-9150); A in Civ (built 2026-11-13, BUG-9170: the editor first and the loading screen live; the capital itself needs a save with a free seat) |
| 1Q | "Player Slots", an Apply button, no Send World button (15) | owner's request, built |
| 1R | The Tribe -> Civ graduation test by scripted native raids | A (done, phase 5 live) |
| 1S | A save loaded with fewer slots than players connected gets more slots | A (built, BUG-9143) |
| 1T | The "Give To" button | B, removed (BUG-9148) |
| 1U | Party wipe: when everyone is down, the newest won village becomes a rival again, the top totem piece and one progress step are lost with a red marker, unlocks and DNA kept, a 10% food debt when nothing was won; Civ: the newest captured city goes back | A; changed by the owner 2026-11-12: it applies when any one player's whole village is wiped out (not rescued in time), and the loss hits the whole party ("a players whole tribe causes the loss of progression for both players, encouraging rescues"); built in Tribe 2026-11-12 (BUG-9155, live by test command); Civ the same (owner 2026-11-12: one player wiped out, i.e. their last city lost, costs the party its newest captured city, no all-players requirement), built 2026-11-12 (live on the MSI by test commands); the red marker in Tribe built and live 2026-11-12 (the lost step's slot red on the bar until won back) |
| 1V | Downed: the creature plays its native dying pose while downed | A; built in Creature / Adventure 2026-11-12 (BUG-9156, live) |
| 1W | Downed: spectate the nearest partner, "SPECTATING <name>", Tab and left / right arrows next to the name | A, with the arrows; built in Creature / Adventure 2026-11-12 (BUG-9156, live); Cell 2026-11-13 (BUG-9171: the camera, SPECTATING, Tab); Tribe, Space not yet |
| 1X | Space: what a player owns | A (owner: "A party will always progress together and share.") |
| 1Y | Space: what phase 6 builds now | A; owner asked how an empire works for a planet that solo players leave (see 2N) |
| 1Z | Space: who runs the enemies in a star system | A |
| 2A | Space: where Space progress is saved | A |
| 2B | Space: how difficulty scales | A; owner asked whether parties need a friend list and whether to switch from slots now (see 2O). Built 2026-11-14 (BUG-9199: the players in the system, raids the players online; docs/re/space.md#space-party-difficulty) |
| 2C | Space: the Galactic Core ending | A. Checked 2026-11-14: the Core waits for the session's partners, which is the party while a session holds one party (cap 4, all seated); the party filter comes with the MMO layer (docs/re/space.md#space-party-difficulty) |
| 2D | Creature: the "cocoon" | the native cocoon (owner: NPC species wrap into a cocoon and come out as a new species; Claude was wrong that the game has none); built 2026-11-13 (BUG-9160: the game's own cocoon animations and effects, live) |
| 2E | Creature: how evolving starts (the owner's full sequence 2026-11-13 in BUG-9181: the game's mating, dance and egg; the family in cocoons while the player edits; back, the family out first, the player hatches from its egg) | owner's rule: native mating call; the nearest mate answers, else one is spawned; the player clicks the mate to start; built 2026-11-13 (BUG-9160, BUG-9161: no vote, the egg scene replaced by the cocoon; BUG-9175: a guest's click ends in the cocoon and the editor, "else one is spawned" as a dead family brought back in seconds) |
| 2F | Creature: DNA when two players evolve at once | B, personal DNA (owner: "Everyone gets their own DNA."); built 2026-11-13 (BUG-9162, live on TITAN) |
| 2G | Creature: who moves the party's nest | owner's rule: anyone starting the native migration warns every player in the world, 10 s later all go into the editor for final touches (downed or dead players too, cleaned up) and migrate together; built 2026-11-13 (BUG-9163; live across PCs with a real migration: one round of editors, the claim, each player in their own new nest) |
| 2H | Space: what ends Civ for a party | A |
| 2I | Space: a party's cities when it leaves the planet | B (owner asked whether a leaving party needs its cities at all, see 2N) |
| 2J | Space: who launches | A |
| 2K | Space: what Space players may do to a planet where others still play Civ | A (owner asked what happens when 100-1000 players fill the galaxy: MMO research, in the Space notes) |
| 2L | Tribe: when the Village page goes | B, now, prioritised (owner: "prioritize this"); page removed 2026-11-12 (BUG-9153); the guest's own village is its player village: Add Baby and the Tribal Planner work natively (live, two PCs); the camera starts at the own village; no Rebuild needed with the party wipe (BUG-9155) |
| 2M | Tribe: the creature editor for a joiner without a creature | B, the editor first, the village after they save; built and live 2026-11-12 (BUG-9159: the host asks, the editor opens, the village follows the save behind a loading screen) |
| 2N | Space: the home of a player or party that leaves a planet others still share | owner's idea: the planet stays protected with one home city that every player's own game shows as their own (their personal empire's home); checked first, fallback a free first colony as the home |
| 2O | Parties: several parties in one session now, or the session stays one party for now | A, later: the session stays one party; no friend list yet |
| 2P | Tribe: a share of the food each player collects for the partners | owner: 25% of each delivery split among the partners in the game, each at least 1 food (even when that is more than was gathered: a party bonus); built 2026-11-12 with food gifts to party villages (BUG-9154, live on two PCs) |
| 2Q | What comes after the overnight work (2026-11-13): A the owner's live checks first (mate icon, Tribe right-clicks, a guest joining without a creature), then Claude builds a Space-ready four-founder save and starts phase 6; B straight to phase 6; C the small leftovers first (2E's spawned mate, a rejoin loading the world twice) | B, straight to phase 6 (owner 2026-11-13; also asked Claude to run the live checks it can itself) |
| 2R | Push feat/lineages-47 (the overnight commits) to the fork: A now; B after the live checks; C not yet | B, after the live checks |
| 2S | The stalled guest mating (BUG-9175): A fix it before Space; B Space first | A (owner 2026-11-13: "This plus the stall needs to be fixed. Investigate if we're over engineering and if it'd be easier to redo anything with what you know now. Let's actively prevent spaghetti coding.") |
| 2T | After the guest mating fix (2026-11-13): A one species key per player creature first (roadmap 1b widened: removes the two-key patches and BUG-9176), then Space; B Space now (2Q B), 1b later; C the owner's live check of the mating first | A (owner 2026-11-13; first the mate's hover card checked with the real mouse on TITAN: "We need this solved.") |
| 2U | Copy the new SporeCoop.package into the owner's Steam Spore install (TITAN's QA games read it): A yes; B no | A (owner 2026-11-13: "Yes"; asked without its ID at the time; the old file in out/owner-install-backup/) |
| 2V | After the downed-box check (2026-11-13): A the 1b cleanup (2T A: remove the patches the player-species fix made redundant); B the player list's name wrap and the downed box in Cell / Adventure first | A (owner 2026-11-13: "2VA"; asked without its ID at the time) |
| 2W | Copy the package with the brighter buttons (BUG-9179) into the owner's Steam Spore install too: A yes; B no (TITAN keeps the game's look until the next install) | A (owner 2026-11-13: "2WA") |
| 2X | 1b, how far (2026-11-13): A one key per player creation on every game, the owner's own game included, made from the owner's persistent identity (removes BUG-9169's copy conversion, BUG-9173's nest note after one migration, BUG-9176); B keys only stable across rejoins (the owner's own game keeps its own key: BUG-9169 and BUG-9176 stay); C Space first, 1b later | A (owner 2026-11-13: "2XA - I feel this is needed for MMO work.") |
| 2Y | The ring of old player nests round the host's nest (left by the old keys: one more nest per rejoin): A the host removes, once per save, the nests the mod made for players that belong to no seat any more; B leave them | A (owner 2026-11-13: "2YA") |
| 2Z | Next after the evolving fixes (2026-11-13): A a short check of Tribe and Civ with the new species keys (founder seats, stand-ins, the right-click orders), then the push (2R B), then Space (phase 6); B Space now; C the push now, then Space | A (owner 2026-11-13: "2ZA") |
| 3A | The 2Z A check found a guest's native game over in Tribe (BUG-9183) and could not test the right-click orders (the test save worn out): A fix the guest's game over and test the orders on a fresh copy of the owner's Jiff save, then push, then Space; B push now, fix after; C Space now | A (owner 2026-11-13: "3AA"). Done 2026-11-13: the guest's game holds the event (BUG-9183, not reproducible on demand); the orders on a fresh copy: Party was refused by the host and is fixed with Hunt (BUG-9185); BUG-9184 found |
| 3B | After the 3A A work (pushed 2026-11-13): A start Space (phase 6) with a Space-ready 3-4 founder save built first; B chase the host's own Tribe game over first (BUG-9184, waits for it to happen again with the new cinematic log); C the owner plays Tribe live first (hunt, gift, attack, raid by right-click from a guest) | delegated (owner 2026-11-13: "Solve everything that shows up with your own recommendations until we reach Space. I'll review your work when I'm able to."): Claude's pick: finish what is left before Space where a test can reach it (the Tribe right-clicks hunt / gift / attack / raid, the Civ late capital), keep BUG-9184 on its support log, then phase 6 (the Space-ready founder save first). Later picks on the way are logged here as 3C, 3D ... with "Claude's pick" |
| 3C | Space is reached with four founders (2026-11-13). Next: A phase 6 steps 2-3 (Space keyed by identity and party, colonies and progress per identity in a side file, enemies run by whoever is in the system), checked on the new four-founder fixture; B the owner plays the four-founder launch first; C the release (Lineages 54) first | A (owner 2026-11-13: "3CA") |
| 3D | Phase 6, enemies per system (1Z): the leader design above is a protocol change in the core fleet sync (several hours, a three-game test). A build it next as designed; B first the smaller identity keying of the allies and rivalry lists (names today); C stop phase 6 here and release Lineages 54 | A (owner 2026-11-13: "3DA"; asked together with the Party panel, the native arrow mockups and a UI scale setting, BUG-9191) Built 2026-11-14 overnight (BUG-9197, fleet.leader): the leader per place runs the NPC ships, the host forwards; live with three TITAN games (mirrors, a shot, a hit, the hand-over; FAIL with the switch off). |
| 3E | The partner arrow (owner 2026-11-13: "should be more native looking"), mockups images/partner_arrow_mockup_A..C.png: A the game's white map pin with the creature's picture and a name plate; B a gold portrait coin like the Party cards; C a slim chevron in the HUD's blue | C (owner 2026-11-13: "3EC"; also: "some of the black backgrounds like behind Fourth doesn't have the thin white outline": the header and the other plates get the native Pack pill's thin light edge) |
| 3F | The Party panel's images are in the new SporeCoop.package; TITAN's games read the installed Steam Data\SporeCoop.package (older: the text list there). A update the installed package now (a backup first, as 2U / 2W); B leave it until the release | A (owner 2026-11-13: "3FA"); done: the old one kept as out/owner-install-backup/SporeCoop.package.2026-11-13-before-party, the panel live on TITAN; 2026-11-13 20:29 the owner asked for the whole build on their install: coop.dll, sporekit.dll, overrides_dc04aee5.sym and the package of 0.7.8-lineages54-dev installed (the previous lineages53 files in out/owner-install-backup/2026-11-13-party/) |
| 3G | The Party UI after the owner's test (2026-11-13, image 4: a native Pack-style card per player at the bottom right beside the avatar card: health and energy bars, name and place plate, portrait): A move to that look (the data side stays: pictures, health, places, the card click; the art from the native avatar card's pixels, the cards lined up with the native card's window; an energy value added to the player state; hover highlight and growth), B keep today's right-side panel and add hover only | A (owner 2026-11-13: "3GA - Be careful of stuff like Space Stage, because there's tools that exist in that area. We'd have to investigate how to do this. Can you create a mockup of it on Space Stage to show proof of concept?") Space mockups (images/space_party_mockup_A/B.png, from the game's Space HUD pixels: the partner cards as small native captain cards with the ship card's bars): A a column on the right above the own ship card, B a row along the top; owner 2026-11-13: "3GA" (the right column). Owner 2026-11-14: "will the Paneling style be accurate to each Stage the player displayed is in? That'd be a fun way to show what stage each person is in": yes, planned: each card takes the native card art of the stage that player is in (Cell, Creature, Tribe, Civ, Space, sampled from each stage's own HUD); in a party all share one stage, so it shows mostly outside a party. Built 2026-11-14 (decision 3K A): a card per partner (the own card is the game's) in its stage's art (tools/party_card_assets.py: Cell, Creature, Tribe / Civ, Space), health and a second bar (Creature hunger, Space energy: PlayerState.energy), stacked at the right edge above Spore's own UI there (measured: the avatar / ship card, the Pack list, the Tribe and Military tabs), a "Party" tab in the own stage's art. Live on TITAN: Space on host and guest, Creature on the host above the Pack list. Not done yet: hover highlight and growth. |
| 3H | A UI Scale for Spore's own HUD (no native setting exists): A a scale transform on the HUD's root windows (minimap, Pack, ability bar, avatar card), as the game's own button growth does, tried on one window first; B the mod's UI only (today) | A (owner 2026-11-13: "3HA - Make sure it's a slider. I want you to create mockups of this being out of the left panel and actually be in one of the settings page. Create a couple option mockups.") Built 2026-11-14 (ui.native_scale, BUG-9191): every top piece of Spore's UI scales about its own screen side; the mouse is moved to the pieces' 100 % places (ns_remap), windows with their own transform (menu buttons that grow) are left to the game; checked live on the MSI at 125 % (menus, Options, Settings and its slider, a HUD button, a world click untouched). Owner said "You can keep going for the deeper fix to ensure it works". Not checked yet: the editors, Space tools, the planner. |
| 3I | Where the UI Scale slider lives (mockups images/uiscale_mockup_A/B/C.png, from the game's Settings pixels): A (Recommended) a page of our own, "Lineages Settings", under Credits, with the slider (50..200 %) and the Frame Rate Cap and Borderless moved there (the left panel empty again, room for later settings); B Game & Capture Settings, a native vertical slider in the empty left of its first box; C Graphic Settings, a row of its own in the quality table | the owner's own layout (2026-11-13, images/uiscale_mockup_owner.png): all on the Graphic page: an "Interface" box under the quality table with the Interface Size slider and its value; the Frame Rate field in the Screen Size box above the resolution; a Borderless Mode check box under Full Screen Mode, the two exclusive ("Fullscreen would turn off if the player enabled borderless and vice versa"); the left panel empty. Plan: the dialog grows a little taller (native boxes moved with set_area) rather than moving the table's radio buttons; the slider is new work (the Audio page's slider copied, or its images with our own drag); it drives the mod's UI first and Spore's HUD once 3H A works. Built 2026-11-14 (ui.settings_layout, BUG-9191): the dialog 72 px taller, the Interface box with a native slider (WinSlider, our flat track and upright knob), Frame Rate with the cache field's look and up / down arrows (owner: 15, 30, 45, 60, 90, 120, 144, 160, 170, 240, 360; blank below 15), Borderless Mode exclusive with Full Screen Mode, even spacing (owner: "a little inconsistent with spacing"). |
| 3J | The Screen Size resolution list (owner, in a side session 2026-11-14, asked what a resolution's Hz means with our frame cap: the cap holds in Full Screen, Borderless and windowed alike; the Hz matters in Full Screen only) | the owner: one entry per resolution, at the highest refresh rate the driver lists for it, the "@ NNHz" text hidden ("So let's default to the highest hz available in the resolution to reduce the size. We'll also hide the Hz text in the resolution list. The player can cap their FPS to whatever they need, or let it increase with it."). Open check: whether Spore uses VSync in Full Screen (sporekit never sets PresentationInterval): Full Screen, cap 144, read the FPS. Built 2026-11-14 (ui.resolution_list): the list drawn short (one per resolution, by size, no Hz; the current resolution keeps its current entry); Spore's mode list is left whole because Spore saves the chosen mode as its place in it ("OptionScreenSize"). Live on the MSI: a pick of 1280 x 1024 saved "@ 360Hz", 1680 x 1050 (scrolled out of Spore's list) too. Not on the owner's install yet (the owner plays the build installed 2026-11-14 00:33, the list off there). |
| 3K | What next after the Settings work (2026-11-14; the owner had not tested yet): A (Recommended) build 3G, the native Pack-style Party cards (right column above the own card in Space; each card in the art of its player's stage); B check Interface Size above 100 % in the editors, Space tools and the Civ planner on the MSI first; C start 3DA, the per-system enemy fleets | A (owner 2026-11-14: "3KA") |
| 3L | What next while the owner tests the Party cards and Settings build (2026-11-14): A (Recommended) start 3D A, the per-system enemy fleets (the squadron leader design, a protocol change in the fleet sync, a three-game test): its own code area, so the owner's test reports can be fixed alongside; B first the cards' polish (hover highlight and growth, live checks in Cell, Tribe and Civ) and Interface Size above 100 % in the editors, Space tools and the Civ planner; C the small check whether Spore uses VSync in Full Screen (cap 144, read the FPS) | B (owner 2026-11-14: "3LB - Please make it clean. I saw some text that looked badly spaced ... definitely take more screenshots and observe the cleanliness of the UI"; the layers sent first: images/party_card_layers). Done: spacing measured on screenshots (6-8 px inside every edge and row, "Party" centred), each stage's art switches at once, the second bar only in Creature and Space, the hover (frame lit, 3 px larger). |
| 3M | What next overnight (2026-11-14; the owner asleep, TITAN's mouse allowed again): A (Recommended) finish the Interface Size checks with TITAN's real mouse (the creature and ship editors, the Space tools, the Civ planner at 125 %), fix what breaks, then start 3D A (the per-system enemy fleets); B start 3D A at once; C the editor checks only, then wait | A (owner 2026-11-14: "3MA plus 3NA. Go ahead and code. Goodnight") |
| 3N | The queue after 3M A, if it is done before the owner wakes (2026-11-14): A (Recommended) in this order: (1) phase 6 step 2, Space keyed by identity and party (captains, allies, rivalry, diplomacy: today network id or name, so a rejoin loses them); (2) difficulty per party and the Galactic Core per party (2B, 2C); (3) phase 7, the Adventure creatures simulated by the host (BUG-9114, waiting for clicks: TITAN's mouse is allowed again); each built, tested, closed and pushed before the next; B stop after 3M A and wait; C (3) first | A (owner 2026-11-14, with 3M A) |
| 3O | After the overnight 3N A work (2026-11-14): phase 7's Adventure enemies (BUG-9114) still need the guest's stale creatures cleaned up (117 unpaired, 81 standing at 0 hp) and the guest's hits tested before the switch can go on. A (Recommended) finish phase 7 next (clean-up of unpaired creatures, the guest-hit test, then the switch on by default); B first install tonight's build on your PC and the MSI so you can test the cards, scale and fleet work; C release Lineages 54 now with phase 7 still off | A (2026-11-14): phase 7 finished and switched on |

Next free ID: 3P. (Owner 2026-11-13: "Make sure you check the instructions given and provide better formatting, especially with decisions." Every question gets an ID, also a yes / no one; A is the recommendation, marked; this line is updated with each new ID.)

Answers of 2026-11-12 (1X-2M), notes:
* 2D: the game has a cocoon. NPC creatures in the Creature stage wrap into a cocoon and come out as a new species
  (SporeWiki, Evolution / Cocoons). The personal evolving uses that cocoon instead of the player's egg: the mating call
  (2E), the player clicks the mate, the creature enters the cocoon, the editor, it comes out as the new creature.
  To find: the cocoon model and the NPC species-evolution code that plays it (docs/re/creature.md).
* 2G with 2F: DNA is personal, the progress (brain) stays the party's; whoever starts the native migration starts it for
  everyone (10 s warning, everyone into the editor, migrate together).
* 2M changes 1P for Tribe: a joiner without a creature goes into the editor first; their village is founded when they
  save (with the loading screen of 1P A). The stand-in village (BUG-9150) stays only as the fallback when they leave
  the editor without saving.
* 2N (owner 2026-11-12): "leave it as a protected planet and maybe have a single city that's client side and is the
  home city for every player". Fits 2A (each player's game keeps its own Space save: it can own the home planet
  locally). To check before phase 6: what a partner's empire uses as its home on your game (the same planet, which your
  game calls yours), and that one city works as an empire's homeworld. Fallback: a free first colony as the home.
* 2P (owner 2026-11-12: "should we split food collected by each player in the party? Like a minority split / tax
  minimum of 1 to all the players and the one who collected gets majority?"): asked with A 10% of each delivery split
  among the partners in the game (fractions, no minimum), plus gifts; B the owner's minimum 1 food to each partner per
  delivery (with 4 players, 3 food off every trip however small); C no automatic share, gifts only. Seen 2026-11-12: an
  idle host's village starved to 0 food while the partner's held 370. Gifts of food to a party member's village
  (owner: "I'm excited to gift food to my party members") are refused today: the host's gift check allows computer
  tribes only (g_order_roster.contains_faction); to build: the game's own gift on a partner's hut moves the food from
  the giver's store to theirs.
* 2K: the galaxy has about 42,000 stars (a community count, TheOnlyZac/starmap); planets are made when a star is first
  visited and almost every system can be colonised (terraforming). A Space empire holds tens of colonies, so about 1000
  players could fill one galaxy. For the MMO: several galaxies (one per server) is the known-safe answer; adding stars or
  planets to one galaxy needs research first (unknown).

* (2026-11-12, both answered: decisions 12 and 13.) Seats added mid-stage: built and live-tested 2026-11-12 on the
  Jiff test copy: 3 reserved, a fourth player refused ("session is full"); `coop.seats 4 now` on the host; the host,
  both guests and then the fourth player read seats=4 (enemy health 130% -> 145%) at once; the fourth player joined
  (4/4) and commanded their old village again (seat 1, by identity).
* The late joiner's creature (owner's question, 2026-11-12): "Why are we having them pick a village and creature on
  late join? Shouldn't they be creating a creature while the village is setup?" Answer: they pick no village (it is
  placed for them); a village (and a capital) is born with its species, so it waits for the creature; the "choose from
  your creations" row was built because the editor path (decision 4's "create") is untested and off. Asked: A open
  the creature editor at once and prepare the site and catch-up meanwhile (the home appears about 15 s after they
  save; existing creations stay as a shortcut), B found the home at once with the host's creature and swap every
  villager afterwards, C keep the choice row with the editor button beside it.
* (2026-11-12, answered "1A": done, phase 5 live-tested.) The Tribe -> Civ graduation test with four founder villages (phase 5), owner 2026-11-12 "2A, be smart": on a fresh
  copy of the Jiff save four founder villages came back in a minute (seat 1 bound with `coop.tribe bind_founder 1 2`),
  but the shortcuts to five wins failed: five totem pieces (Totem_AddSegment) plus the player's progress at 5 / 5
  (cPlayer +0x10F0) do not make History offer the next stage; an NPC village made with `coop.tribe npc_tribe` and its
  hut destroyed with the host's faction (cTribeHut_TakeDamage) counts as no win. The game offers Civ only after a native
  win through its goal path (a target chosen by TribeGoalUI_SelectTarget, raided by ordered villagers:
  tools/tests/duo_tribe_campaign.py). Asked again 2026-11-12.
* (answered 2026-11-12, "2A" and "3B", built: BUG-9143, BUG-9148.) BUG-9143: a save loaded with fewer player slots
  than players connected now gets more slots; the "Give To" button is removed.
* Party wipe (owner 2026-11-12: "When all players go down or die in a stage like tribal ... lose a totem/progress and
  a captured location with the red debt icon ... They keep their unlocks and DNA ... This can work in Civ as well."):
  researched 2026-11-12, design and its decisions asked (when it applies, which village, how it comes back, the totem,
  none won yet, Civ). Note: the native "bounce back" reloads the slot the mod autosaves every 10 minutes and after
  every win, so today's loss is up to 10 minutes, not the wins.
* Space and the MMO goal (owner 2026-11-12: "We still need to discuss Space Stage"): briefing sent 2026-11-12 with six
  decisions (what a player owns in Space, phase 6 scope, who runs enemies, where Space progress is saved, difficulty
  scaling, the Galactic Core).
* Downed players (owner 2026-11-12): the dying animation while downed, spectating a partner with a clear HUD and a
  native vignette. Proposal asked.
* Creature: a personal editor with the native cocoon (owner 2026-11-12: "Now that we'll have personal nests, we should
  allow each player to enter the creator on their own accord. While a player is in the editor or evolving ... apply
  the evolving cocoon native to the game. They will enter the cocoon with the native animation when starting the
  editor. When they confirm their creature, they can pop out of the cocoon with the native animation with the new
  creature. It should be very natural and immersive for an outside viewer of the nest."). Replaces the evolution vote
  in Creature. Research 2026-11-12: Spore's Creature stage has no cocoon; its own evolving sequence is the mating dance
  (hearts), an egg in the nest (cEgg, its laid / hatched effects), the editor, then the creature hatches as a baby and
  grows up (CreatureGame_AfterEditor, GrowTo). Partners today see the editing player's puppet vanish and come back
  adult-sized. Decisions asked 2026-11-12 (the egg as the "cocoon", how evolving starts, the host's editor, DNA when two
  evolve at once, migration). A likely bug found on the way: BUG-9151.
