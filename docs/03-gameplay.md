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
   The slot is a copy of the host's whole galaxy (every planet of the host's `GameN`, old games
   too). A copy the player saved in while not a guest (alone, or hosting from it) gets an
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

