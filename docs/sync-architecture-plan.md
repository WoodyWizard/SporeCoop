# Why co-op desyncs keep coming, and the 0.7.4 plan (2026-11-02)

Input: the 15 reports of 2026-11-02 (BUG-0453..0467), the ticket audits of BUG-0001..0414 (out/ticket-audit-*.md),
docs/generalisation-plan.md (classes G1-G16), docs/re/host-centric.md. Numbers from the repo on 2026-11-02:
466 tickets (69 kind desync, 207 bug, 57 crash), 293 of them say "again", "sibling" or "same class";
54 message types; 239 `COOP_FIX` switches; 10 modules with their own `on_world_change`, 11 with `leaving_stage`.

## 1. How the mod works today

Every player runs a **complete, independent Spore** on a copy of the host's save. Spore has no network
model and is not deterministic, so the games cannot run in lockstep. The mod keeps them close by
**copying a chosen list of things** from one game into the other:

* the partner's avatar is a **puppet** on my screen (a game creature the mod drives from MSG_PLAYER_STATE / MSG_ANIM);
* the host's NPCs are **proxies** on the guest (MSG_NPC_SPAWN / STATE), the guest's own NPCs are removed;
* one message type per feature for the rest: damage, DNA, parts, nests, social, species, cell food, tribe, civ, space...

Everything is opt-in: a game system is shared only if someone wrote a message, a sender, an apply
path and a world-change reset for it. 54 message types were added this way, most of them after a
player report.

## 2. The pattern: six structural causes

Each of today's reports falls into one or more of these. They are causes in the architecture, not
in one feature, which is why fixing a report one by one produces the next one.

| # | Cause | What it means | Today's reports | Older classes |
|---|---|---|---|---|
| A | **Divergence is the default** | Anything not on the copied list silently lives a separate life in each game. Nobody has the list of what is NOT copied: it is found by players. | 0458 stick, 0459 bone, 0460 nest extinct, 0464 nest evolution, 0465 kraken, 0451 effects | G4 |
| B | **The game knows one player** | On my machine the partner is a puppet, an ordinary creature. All "player-only" game code (abilities, sneak, roar, social, effects, eating animations, pickups, brain HP, player species) runs only on the owner's machine; its results stay there unless relayed. | 0462 roar, 0463 sneak, 0451 effects | G1, G12 |
| C | **Mod-built copies are incomplete** | Puppets and proxies are created by the mod, not by the game's normal spawn, with only the fields the mod knows. Unknown fields keep defaults. | 0461 babies as adults (scale sent, never applied), 0457 old creature version | G2, G15 |
| D | **No single owner per object** | Ownership is decided per feature: NPCs host, family / eggs / pickups / nests guest-local, parts a union, DNA a ledger, relations local. An interaction that crosses owners breaks (guest touches a host object, host object has a guest-local part). | 0459, 0460, 0462, 0467 | G3, G4 |
| E | **Events instead of state, no repair** | Most messages are one-shot events ("hit", "add parts", "spawn once"). An event lost or ignored at the wrong moment (editor, load, unknown id, mode mismatch) leaves a divergence forever. sync-check only measures, it never repairs. | 0466 DNA back after editor, 0457 | G5, G6 |
| F | **Every module handles transitions itself** | Editor, evolution, death, reload and stage change are re-implemented per module (10 + 11 hooks). Desyncs and crashes cluster right after transitions. | 0457, 0466, 0343 off-centre after evolve, crashes 0454 / 0455 in editors | G10, G11 |

Process cause on top: a fix is a per-symptom switch (239 of them) with a per-feature test. No test
asks "is the other player's world the same as mine", so siblings are only found by players.

## 3. The plan: four layers instead of more messages

### L0. A divergence detector (first, before any code change)

A lab tool that dumps **the game's own object list** on both machines (every noun: type, owner,
position, scale / age, hp, alive / dead, state flags, model key, carried-by) and diffs them,
per stage, with tolerances. Run it in every two-instance test as a gate (`monitor.py` rule
`world_diff`), and in a long soak per stage. Kinds that exist on one machine only are unsynced
systems found by the lab, not by players (cause A). Output: the **sync matrix** below, generated,
not written by hand.

### L1. One ownership rule (cause D)

Every world object has exactly one owner: **the host** for world objects (NPCs, nests, pickups,
carried objects, monsters, food), **the player** for its avatar and what the avatar holds.
Non-owners do not simulate the object, they mirror it. Guest-local objects only by an explicit list
with a reason. Any action on an object you do not own is a **request to the owner** (pick up, eat,
hit, roar, social), the owner runs the game's own function, and the result comes back as state.
One generic `request(object, action)` path instead of one message per action.

### L2. Replicated state instead of events (causes C and E)

One replication layer for every object kind: a descriptor per kind lists its fields, each with a
`read` from the game and an `apply` into the game (lint: a field without apply cannot exist, so
"sent but never applied" is impossible). Snapshots with deltas, a periodic full refresh, and a
resync request for any unknown id. Ledgers (DNA, parts, money) become owner state with a version,
never dropped. Adding a field is one table row. Proxies and puppets are built from the full
descriptor, through the game's own spawn where it exists.

### L3. Player-ness relayed at the chokepoint (cause B)

Do not copy abilities one by one: hook the game's few chokepoints that player-only code goes
through and relay their output to the puppet. First: effects (BUG-0451, the effect creation
functions), then the avatar's state flags (sneak, stealth, ability in use), animation events, and
the PlayerSpecies rules for the partner (G1). The rule: hook where the game creates the result, not
where one ability starts.

### L4. One lifecycle service (cause F)

One module owns session / world / stage / editor / busy transitions and tells every other module
through observers. When a transition ends, L2 does a **full resync of every kind**. Editors,
evolution, death and reload stop being special cases in each module.

## 4. Scope decision (owner, 2026-11-02)

**0.7.4 = Cell and Creature** on all four layers. Tribe, Civ and Space follow in later versions with the
**same layers and the same rules**: the differences between stages are allowed only inside a stage
adapter, never as a new mechanism. The owner does not want to wait for player reports for any stage,
so the detection part (L0 and the discovery work below) runs on **every stage from 0.7.4 on**.

### Stage-independent design

The layers are written once, in stage-free code; a stage supplies only an adapter:

* L0: the object dump and diff are generic; a stage adds its object kinds and tolerances.
* L1: `request(object, action)` and the owner rule are generic; a stage lists its actions and the
  game function that performs each one.
* L2: the replication engine (snapshot, delta, full refresh, resync of unknown ids, versioned
  ledgers) is generic; a stage supplies one descriptor per object kind (fields with read / apply,
  spawn through the game).
* L3: chokepoint hooks are game-wide (effects, animation events, ability flags); a stage only says
  which local objects are "the player's" (avatar cell, avatar creature, own tribe members, own
  vehicles, own ship).
* L4: one lifecycle service for all stages.

A new stage is "done" when its adapter exists for every kind the detector finds and the detector
shows no difference in its scenarios. Old per-stage messages are retired one by one behind switches.

### Finding problems before players do (all stages, from 0.7.4)

1. **Detector in every two-instance test**, all stages, as a gate (monitor rule `world_diff`).
2. **Scripted playthroughs per stage** (bots on both games doing what players do: eat, fight,
   social, pick up, carry, editor, evolve, die, reload, join late, rejoin) with the detector on,
   on the lab nightly. Every one-side-only kind or field becomes a ticket automatically
   (`tools/tickets.py new --source detector`).
3. **Inventory from the game, not from reports**: per stage, list the game's object kinds (nouns),
   player-only code paths (callers of the player / avatar / PlayerSpecies accessors) and ability
   functions from Ghidra; each entry gets a row in the sync matrix (owner, layer, test, status).
   An entry with no row is a known gap before any player meets it.
4. A player report that the matrix did not predict is a bug in the inventory: add the missing row
   and the detector field, not only the fix.

## 5. Order for 0.7.4

| Step | What | Fixes or proves |
|---|---|---|
| 1 | L0 detector (generic) + adapters for Cell and Creature; dumps also for Tribe / Civ / Space (report-only) | finds the unknown list (A) on every stage |
| 2 | L4 lifecycle + full resync after transitions (all stages) | 0457, 0466, 0343, part of the editor crashes |
| 3 | L2 engine + Creature descriptors (pilot): puppets, proxies, nests, pickups, carried objects | 0461, 0457, 0460, 0458, 0459 |
| 4 | L1 requests for Creature (pickup, eat, hit, roar, social) | 0459, 0462 |
| 5 | L3: merge the effects relay, then sneak / ability flags (game-wide) | 0451, 0463, 0462 roar |
| 6 | Cell on L1 / L2 with the engine proven by the pilot: cells, food, part pickups, KO, hits | 0467, 0343, Cell classes |
| 7 | Kinds the detector finds in Cell / Creature (kraken 0465, nest evolution 0464, ...) | A |
| 8 | Inventory + scripted playthroughs for all stages; tickets for Tribe / Civ / Space gaps | the next versions' lists |
| 9 | Crashes 0453-0456 in parallel (G9 lifetime, editor) | |

Then: Tribe, Civ (0.7.5), Space (its own galaxy model, 0.7.6), each through the same adapters.
Urgent crash fixes ship as 0.7.3.x / 0.7.4.x hotfixes instead of waiting. Risks: host CPU and
bandwidth for full-state replication (measure with 4 players), objects the game cannot spawn on
request, and the size of the change: old paths stay behind switches until the new layer passes
the same tests.

## 6. Today's reports mapped

| Ticket | Cause | Step |
|---|---|---|
| BUG-0457 old creature version after host's editor | C, F | 2, 3 |
| BUG-0458 stick not synced | A, D | 3, 4 |
| BUG-0459 bone picked up twice | A, D | 3, 4 |
| BUG-0460 nest extinct / heal | A, D | 3 |
| BUG-0461 babies as adults | C | 3 |
| BUG-0462 relations, roar | B, D | 4, 5 |
| BUG-0463 sneak | B | 5 |
| BUG-0464 nest evolution | A | 7 |
| BUG-0465 kraken | A | 7 |
| BUG-0466 DNA after editor | E, F | 2, 3 |
| BUG-0467 Cell part pickup | D | 6 |
| BUG-0453..0456 crashes | lifetime / editor | 9 |
