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
| DNA | host's shared pool: a kill/befriend by any player gives DNA to everyone within 50 m; each gain counts 1/N with N players in the world, so the party evolves at the single-player pace (`dna_pace=1` in `mods\coop.ini` [coop] on the host; 0 = every gain whole, BUG-0510) | single progress bar |
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
