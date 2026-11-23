# 3. Co-op gameplay

## 3.1 Concept: "Posse" (Creature stage)

The **Creature stage** was chosen as the first target because it is the most co-op-friendly
by nature: Spore already has a posse mechanic (up to 3 allies near the player, with their
health shown in the UI), combat, socializing and shared progress through DNA.

Co-op turns **other players into members of your posse**: 2–4 players (host + up to 3 guests:
exactly as many as the game's posse slots), each controlling their own creature; the world
and progress are shared, and the world belongs to the host.

Since M5, co-op works in all modes; see [7. Co-op in all modes](07-modes.md).
Below is the original analysis we started from (the Creature stage is still the most complete).

Why not the other stages (initially):

| Stage | Problem | When |
|---|---|---|
| Cell | 2D world with a rigid single-player camera | not planned |
| Tribe/Civilization | RTS controls, huge amount of unit/building sync | later, possibly "shared control" |
| Space | each player has their own galaxy save; but the star map is identical | "Wingman" stage (see 3.8) |
| Adventures (GA) | good fit: small scenarios, captains | second target stage |

## 3.2 What a session looks like

**Host**
1. Loads their Creature stage save (or starts a new one).
2. Opens the cheat console `Ctrl+Shift+C` → `coop host` (default port 47810).
3. Top-right corner shows: `CO-OP: host · 0/3 players · port 47810`.

**Guest**
1. In the main menu: `Ctrl+Shift+C` → `coop join 192.168.1.10` (`coop name Nick` beforehand).
2. The mod checks compatibility (mod version, exe build), receives a **world snapshot**
   from the host (the save folder, a few MB), puts it into the slot
   "Co-op: <host>" and loads it (milestone M1: the guest loads the slot manually).
   Where it goes (mods/coop/slotfs.cpp): the co-op slot of the same host and planet (its
   `sporecoop.txt` names both), else a free `Games\GameN`; never the slot the game plays. The
   files are written into `Spore\Games-sporecoop-tmp` first and moved into place as a whole; a
   world that could not be written completely is neither kept nor loaded. A load of a received
   world waits while an editor is open and is dropped when the session ends.
   Up to 0.7.16.1 the slot was a copy of the host's whole galaxy (every planet of the host's
   `GameN`, old games too); since 0.7.17 a share sends the played planet's `.spo` and the galaxy's
   own files only (a guest with world 1.1 or newer; `coop.fix world.share_planet`, BUG-1434). A host
   in the galaxy menu gets **Continue <planet>** in the co-op window for the co-op copy it played
   last (`coop.continue`, `coop.fix world.continue_coop`). A copy the player saved in while not a guest (alone, or hosting from it) gets an
   `own_save` line in its `sporecoop.txt` and is the player's own from then on: no later share
   reuses, replaces or tidies it (0.7.16.1, BUG-1434). A host whose save database points to such a
   copy of another player's world is told whose copy it is and which planet it was made for.
   The host waits for the guest's load of the world it sent (its pass through the galaxy menu),
   instead of taking that pass for a leave to the main menu (BUG-1435).
3. The guest appears next to the host's creature: "<Nick> joined the posse".

**In game**
* Each player has a nickname and a health bar above them; guests occupy the host's posse
  slots (their health is visible in the standard posse UI).
* Chat: `coop say <text>` (later - the Enter key in the overlay), feed in the corner of the screen.
* Guest disconnect: their creature "leaves" (dissolves), freeing the posse slot.

## 3.3 Shared world rules (host is authoritative)

| What | Who decides | How others see it |
|---|---|---|
| Movement of your own creature | each player locally (responsiveness) | 100 ms interpolation |
| NPC creatures, nests, eggs, fruit, epics | **host** (AI runs only on the host) | puppets on the guest within ~150 m |
| Guest attack/ability on an NPC | guest sends "ability X on target Y", host computes damage | animation for everyone, target health from the host |
| Damage to a guest from an NPC | host | guest receives a damage event |
| Guest death | - | respawns at the host's nest after 10 s |
| DNA | own DNA per player (0.7.16, BUG-1291, [own DNA](#gameplay-own-dna)): a kill/befriend by any player gives DNA to everyone (every player's DNA rises alike), each player's editor spends only its own; the session setting `all.shared_dna` brings back the host's one shared pool. Each gain counts 1/N with N players in the world, so the party evolves at the single-player pace (`dna_pace=1` in `mods\coop.ini` [coop] on the host; 0 = every gain whole, BUG-0510) | the party's progress bar |
| Body parts (bones, finds) | shared unlocks | - |
| Mating call → editor | host decides when; guests wait in an "editor lobby" | M4: everyone edits their own creature |
| Transition to the Tribe stage | session ends cleanly with a message | - |
| Pause | host: pauses for everyone; guest: local menu only | - |

Players trust each other (co-op with friends): there is no anti-cheat, the guest reports its own position.

## 3.4 Guest appearance

* M1: the guest looks like a creature of the **host's species** with a different color/size (the
  species is guaranteed to exist in both worlds - it's the host's world snapshot).
* M4: the guest picks their own creature from the Sporepedia; the creation file (`.crt`, a few KB)
  is sent to the host and the other guests and registered in their resource database.

## 3.5 Development milestones

| Milestone | Contents | Done when |
|---|---|---|
| **M0 Infrastructure** | loader, SporeKit, devkit, sporectl, build, deployment to Bottles, symbol database | the game starts with mods, `sporectl info/shot/cheat` work |
| **M1 "Ghosts"** | networking, host/join session, remote player puppet: position, rotation, walk/run/jump animations; nicknames; fake peer | the guest's creature walks around the host's world (or `fakepeer.py`) |
| **M2 "Posse"** | guest = posse member on the host; NPCs on the guest are host puppets; ability and damage events | a joint fight with one NPC looks the same for both |
| **M3 "Shared progress"** | DNA, parts, death and respawn, eggs/nests | the guest can kill an NPC and the host gets DNA |
| **M4 "Convenience"** | automatic world transfer and loading, guest's own creature, menus instead of cheats, editor | joining a session takes 1 command |
| **M5 Adventures** | co-op in Galactic Adventures: captains, shared act goals | - |

## 3.6 What works already (2026-10-20)

* Host/guest, nicknames, chat, action animations, transfer of the host's world to the guest.
* NPCs exist only on the host; the guest sees their reflections within 110 m, guest hits and
  damage from NPCs go through the host. Time of day and shared DNA are synchronized.
* Social games of a guest (2026-10-23): in the social stance the guest clicks a reflection and
  presses a social ability key (1-4); the guest's creature performs it and the host counts the
  rounds the native game would ask for (1 for a Kook, 2 for an alpha), then applies the native win
  (the species counts one more impressed creature, the pack gets the creature's DNA, the NPC is
  impressed and plays its reaction on both screens). The goal cards (impress / ally / hunt /
  extinct) come from the host's species table. The native meter and the NPC's ability prompts are
  not shown on the guest (docs/re/creature.md 8.3).
* Not synchronized yet: eggs, nests, fruit and body-part finds, guest appearance (host's species).

## 3.7 Limitations we are upfront about with players

* The guest plays in the **host's world**: their own save is not changed.
* A session exists only in the Creature stage; the creature editor is single-player until M4.
* Direct IP connection (UDP port 47810 forwarding or a VPN such as Tailscale/ZeroTier).

## 3.8 Future idea: "Wingman" in Space

All players share the same star map, so a guest in Space can
"fly into" the host's system: the host's UFO is visible, joint fights with pirates/epics,
shared events in the system. Requires separate RE of the space simulator.

## Own DNA per player (2026-11-19, feat/w56-dna)
<a id="gameplay-own-dna"></a>

WP-D of docs/plan-0.7.16.md, BUG-1291, owner decision 2026-11-19 ("each player has its own DNA by default; a session
setting turns on the old shared pool; Rivals always separate"). Model "party income, own wallet" (design: the Lineages
fork's idea, ported per out/research/lineages-systems.md section D; their MSG_DNA_OWN and guest-side worlds.ini are not
ported). Code: `mods/coop/dna_own.h` (portable, tests/dna_tests.cpp), `dna.cpp` (the host's wallets), `session_glue.cpp`
(MSG_DNA / MSG_DNA_ADD), `cellsync.cpp` + `wsync.cpp` (Cell).

| Question | Answer |
|---|---|
| What a guest's editor spends from | its own game's DNA (GetEvolutionPoints in Creature, DNAForFood(food) - S+0x2C in Cell), which the host's MSG_DNA sets to the guest's own wallet; the spend goes to the host (MSG_DNA_ADD < 0, BUG-0466's path; Cell: MSG_CELL_FOOD's spend, BUG-1249's path) and comes off that guest's wallet only, never off the host's game |
| Income | every gain of the party (anybody's, the game's own rewards, a guest's forwarded gain, owed and solo DNA, goal cards) raises the host's evolution bar; each frame in Creature the host adds its bar step to every wallet of the world. Cell: the food is the party's (MSG_CELL_FOOD), so DNAForFood(food) is everybody's income |
| The progress bar | the party's: the host's cPlayer +0x10F0 goes in MSG_DNA's tail, a guest raises its bar to it (never lowers it) and then sets its budget with SetEvolutionPoints |
| The host's save | only the host's own DNA and the party's bar (the game's own fields); nothing of a guest's wallet is written into the host's game, so no save strip is needed |
| A guest's DNA kept | in the host's memory and the host's per-player store (record `dna.own` v1: the Creature budget and the Cell spent count with their worlds), written after every save of the host; read when the host's world opens; a reconnect finds it by the guest's stable key; a reload of the host's world rolls it back with the host's own DNA |
| First sight | a guest without a wallet in this world starts with the host's DNA (Cell: the host's spent count): an old shared-pool save gives every guest what it saw before |
| Shared setting | `all.shared_dna` (All stages, Session, default off = own; read as off in Rivals mode). Also shared: `creature.mode` host (one creature, only the host evolves) |
| A 0.7.15.2 peer | a guest whose mod is older (creature / npc family minor < 2) plays the shared pool with the host as in 0.7.15 (it gets the host's total, its spends come off the host's DNA); a new guest of an older host gets no tail and plays the shared pool. Nothing new reaches an old peer but the trailing bytes after MSG_DNA's pace byte (read by nobody there) and the wsync row id 19 (skipped as unknown) |
| Switches | `creature.own_dna` (off: one pool, 0.7.15), `creature.dna_bar_set` (off: the guest moves its DNA with AddEvolutionPoints: its bar runs ahead after a reload), `creature.own_dna_store` (off: a guest starts again at the host's DNA after the host's world opens again) |
| Pace | `all.dna_pace` paces the party's income: with it on every player has a solo game's DNA; with it off every player gets every gain whole (N times a solo game's DNA) |

Not covered: a guest's own editor opened in the first 2 s after its game loaded the host's world shows the host's
saved DNA until the next total (the host resends every 2 s); Rivals' own income ("keep what I hunt", model O) is 0.8.

## Own progress: the third DNA mode (2026-11-23, feat/w69-progress)
<a id="gameplay-own-progress"></a>

WP-P1 / P2 of docs/plan-0.7.17.md, BUG-1469, owner decision 2026-11-20 (a lobby setting with three values). The idea,
the "waiting for X 64 %" notice, the stage-end gate, the social-win credit and the brain level's name come from PR #209
by IltonBJSilva; the storage is ours (the per-player store, not a side file), and the Cell gate sits on our one hook of
the end sequence (`SporeCoop::CellStage_End`, BUG-1269), not a second one. Code: `mods/coop/own_progress_logic.h`
(portable, tests/dna_tests.cpp), `own_progress.cpp`, and the steps aside in `dna.cpp`, `session_glue.cpp`,
`cellsync.cpp`, `cellsync_combat.cpp`, `wsync.cpp`, `social.cpp`, `vote.cpp`.

| Question | Answer |
|---|---|
| The setting | `all.shared_dna`: 0 own wallet (default, [own DNA](#gameplay-own-dna)), 1 shared, 2 own progress; the lobby row "DNA and progress"; own wallet in Rivals mode. Read with `settings::get()` (on() would read 2 as shared) |
| What is own | Cell: the food S+0x1C (the bar and the level) and the spent count S+0x2C; Creature: the DNA, the evolution bar cPlayer +0x10F0 and the brain level (`SporeCoop::g_BrainLevel`). Each game keeps its own: no MSG_DNA total, no Cell food total applied (only the meals' acknowledgement), no 1/N pace (each bar fills at a solo game's pace), a spend stays the spender's (no MSG_DNA_ADD, no Cell spend message, the wsync row cell_spent not applied) |
| Income | a game's own gains stay there (its meals, the game's rewards, its own goal card: each game pays its own card). What the host's game pays for a guest's deed goes to that guest (MSG_PROGRESS kGain): a kill of a host animal (the hunt credit of npc.cpp, 2.5 s, or an Actor scope of WP-H1), a social win (social.cpp: the host's game no longer adds it to its own DNA; the guest takes it from the result) |
| The stage end | nobody evolves alone. Cell: the host's click on History's check is kept before the game's end sequence (History closes, the pool stays drawn) until every player's bar is full; then it runs by itself and the switch vote follows. A guest's click in the host's world is never its own switch (its I'm ready during the vote, else a notice). Creature: the host's stage-end request (tag 0x64AA168) is refused until every bar is full (brain level 4), then BUG-1085's vote. A second click within 10 s moves on without the others (a partner away from the keyboard): the vote decides (it passes when its time is up). Guests in another world are not waited for |
| Levels together (Cell) | the one pool is the host's game, populated at the host's level (PR #209's BUG-9287: a guest a level below found nothing to eat). An own meal over the next level line (every 100 food) while a partner is below it is clipped; the rest is banked and added (in the cell step, `cellsync::set_food_total`) once every partner reached the line; level 10 (900 food) is not clipped (the stage end waits instead). A partner who left this session keeps its level until it is back |
| Notices | "Your evolution bar is full. Nobody evolves alone ... Waiting for: Bob 64 %" (again every 60 s), "Every player's bar is full ...", "Not yet: ... (waiting for: ...)", "Nobody grows alone: your cell waits at level N until ..." |
| Kept | a guest reports its values every second (kReport); the host keeps them by the guest's stable key and in the per-player store (record `progress`, [the table](01-architecture.md#arch-player-store)); a guest whose game opened a copy of the host's world (a join, a rejoin, a reload, the next session) asks (kAsk) and gets its own values of this world and stage back (kSet: food and spent; the bar up through AddEvolutionPoints, down written; the DNA set; a lower brain level written back). First sight (no record): the copy's values (the host's) are its own from then on |
| The host's save | only the host's own values (the game's own fields); a guest's progress is never written into the host's game |
| Old peers | the gate is creature 1.3 and cell 1.2 (`own_progress::peer_reads`). A 0.7.16.1 peer in the session when the host's world starts with value 2: own progress is refused, own wallet is played, the host is told who; a late joiner below it while own progress is played is not let in ("update SporeCoop to 0.7.17"). The host tells its guests whether own progress is played (MSG_PROGRESS kTable flag): a guest plays it only on the host's word |
| Switches | `core.own_progress` (off: value 2 plays as own wallet), `cell.end_click` (off: the click held after the end sequence: the white picture of PR #209's BUG-9266 stays), `cell.level_together` (off: each level on its own), `core.progress_restore` (off: a guest keeps the host's values after a load) |
| Tests | tools/tests/coop_own_progress.py (suite `own_progress_*` and their twins) |

Not covered (risks): the hunt credit's 2.5 s window credits a host's own kill in it to the guest (the Actor scope of
WP-H1 is the exact way); a lower brain level written back keeps the avatar's larger size and health until its next brain
upgrade (PR #209's runtime note); a Cell food a guest ate that another player ate first is not taken back (the host's
total no longer corrects a guest's food); BUG-1261 nit 7 (a guest's TierBoneCleanup) is not checked under own progress
yet; the debt column of `progress` is WP-R1's ([revive](#gameplay-revive), BUG-1472: a revive's price under own progress).

## Revive a downed player (2026-11-23, feat/w71-revive)
<a id="gameplay-revive"></a>

WP-R1 of docs/plan-0.7.17.md, BUG-1472. The rule header (the window, the revive distance and time, give-up, the party
wipe clock), the K_DOWN / K_RESULT / K_LINES flow, the "DOWN 55 s" tags and the pausing caption's kill retries come
from PR #214 by IltonBJSilva (its Creature part); the price is ours (the downed player's own, not 10 % of the host's
DNA), the respawn is the game's at the shared nest (not #208's own nest), the pack rescue stays the first line. Code:
`mods/coop/revive_logic.h` (portable, tests/revive_tests.cpp), `revive.cpp`, `give_up.h`; the order in
`teamplay.cpp` on_own_damage; the debt in `own_progress.cpp`; tags and arrows in `coop.cpp`; the save hold in `vote.cpp`
save_blocker.

| Question | Answer |
|---|---|
| The setting | `creature.revive` (Creature, session): on by default (provisional, `settings::kReviveDefault`, owner question 4); off: the hit kills as in 0.7.16.1 |
| Who catches a lethal hit | every game guards its own creature (teamplay::on_own_damage: the host's own hits, a guest's own hits, the host's MSG_DAMAGE on a guest). First the pack rescue (BUG-0099: a partner within 15 m, once a minute: 40 % health). Else, with a partner playing in the same world (a fresh state, its creature in it) and, on a guest, a host of creature 1.3: down (`revive_logic.h` catch_hit). Alone: the game's death |
| Down | 1 health, stopped, put back where it fell when it moves more than 0.4 m (not during a cinematic), no hit lands (switch `creature.revive_hold`), no pack regen; 60 s. "X is down! Stand next to them within 60 s to revive them." to everybody |
| Revive | a standing partner (not down) within 6 m for 1.5 s (the host's own creature or any partner's state, measured on the host): up with 40 % of its maximum health, 2 s without hits after it |
| No revive | the window runs out, its player holds G for a second (gives up, `coop.revive giveup` in tests) or the game kills it another way (a path past the damage filter): the game's own death (the caption, the respawn at the nest). While a pausing caption keeps the game from taking damage, the lethal hit is tried again every second (kill retries, PR #214's lane D finding) |
| The price (host) | `revive_logic.h` price_for, 10 % of the downed player's own DNA rounded up (`kCostShare`, owner question 4): own wallet: its own wallet (the host's game DNA, a guest's book through dna_own_spend; the next MSG_DNA carries it); shared: the shared pool (the host's total); own progress: a debt on its bar (the `progress` record's debt column): its next own gains pay it first (DNA and bar, on_dna and take_gain), "Your revive debt: your next N DNA pay it first". Switch `creature.revive_debt` off: PR #214's rule, 10 % of the host's DNA |
| Party wipe | every player down for 2 s: every window ends at once (each dies the game's way) |
| What everybody sees | over a downed partner "DOWN 55s: Bob (30%)" in red (also with name tags off), its arrow "Bob (DOWN 55s)"; the downed player's own screen a red line "DOWN 55s: a partner next to you revives you. Hold G to give up." |
| Saves | no world save of the host while a window is open ("a player is down: wait for the revive or the respawn"; shares, autosave and `coop.save` wait) |
| Old peers | MSG_RESCUE 77 is creature 1.3 (`revive::kMinor`): a guest goes down only under a host that reads it (else it dies as before), the host ignores K_DOWN of an older peer and sends its lines only to peers that read them. A 0.7.16.1 peer plays as before; it can still revive a downed 0.7.17 player (the host measures its position) |
| Switches | the setting, `creature.revive_hold`, `creature.revive_debt` |
| Tests | tools/tests/coop_creature_revive.py (suite `creature_revive*` and the twins `creature_revive_setting_off`, `creature_revive_hold_off`, `creature_revive_debt_off`); tests/revive_tests.cpp |

Not covered (risks): damage paths past the damage filter during the window (drowning / the sea monster, falls, an epic's
stomp) end it with the game's own death (paths row of the test: a fall is held, deep water either held or the game's
death that ends the window); a downed creature in a cinematic is not put back (BUG-1173's class); the debt of a guest is
restored only for the same world (its record's world); the hunt credit of a downed player is not stopped.

## Difficulty for 3-4 players (2026-11-23, feat/w72-difficulty)
<a id="gameplay-party-difficulty"></a>

WP-D1 of docs/plan-0.7.17.md (item 5), BUG-1473. With three or four players the NPCs fall too fast; the session setting
`all.party_difficulty` (lobby: "Difficulty for 3-4 players", All stages) scales the fights. Values: 100 % (a solo game's
balance), 125 %, 150 %, 200 %, auto (+25 % per player over two: 3 players 125 %, 4 players 150 %, 2 players 100 %; it
follows the session's players as they join and leave). The default is 100 % until the owner answers question 5 of the
plan (100 % or auto): `party::kDefaultChoice` in `mods/coop/party_difficulty_logic.h` is the one line to change.

* At P %: a player's hit on an NPC deals damage x 100 / P (an NPC takes P / 100 times the hits to fall); an NPC's hit on
  a player deals damage x P / 100. NPC on NPC, player on player (friendly fire, Rivals) and hits without an attacker (a
  fall, the sea) are the game's own.
* The host applies it (it simulates every NPC); a guest's hits on the host's NPCs arrive as MSG_DAMAGE_REQ and are
  scaled there, the NPCs' hits on a guest go to it already scaled. A guest's game never scales (an older guest plays the
  host's scaled hits, an older host none: the setting goes by name, MSG_SETTINGS, no wire change).
* Where: Creature (and the wild animals of every stage): npc.cpp damage_filter and MSG_DAMAGE_REQ; Tribe: the citizens'
  TakeDamage (tribe.cpp h_CitizenTD: the player tribe's members against other tribes, animals and epics); Civ: the
  vehicles' TakeDamage by owner (civ_shots.cpp) and a player's vehicle's hit on a building (civ.cpp b_damage; an NPC's hit
  on the players' buildings is the game's, the building's owner is not read there); Space: the UFOs on the host (the host's
  ship and the guests' puppets against NPC ships, the guests' MSG_UFO_HIT). A place a guest leads (BUG-1288) is not
  scaled. Cell: not scaled (its fights are cellsync's own paths).
* Why not the game's difficulty: its per-mode lists hold no NPC health or damage key, the readers cache them at a mode's
  start, and Space's keys are the economy ([RE](re/creature.md#creature-difficulty-tunings)). The game's own difficulty
  (easy / normal / hard, cPlayer +0x1248) is untouched and still applies on top.
* Switch `coop.fix core.party_tuning off`: every hit is the game's (0.7.16.1). `coop.party`: the percentage in force and
  the hits it scaled (the setting itself is in the support report's settings diff).
* Test: tools/tests/coop_party_difficulty.py (three games; suite party_difficulty, party_difficulty_auto, twin
  party_difficulty_off), unit tests tests/party_tests.cpp.
