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
| 1 | L0 detector (generic) + adapters for Cell and Creature; dumps also for Tribe / Civ / Space (report-only). **First version done 2026-11-02** (feat/w39-detector, [06-testing.md#testing-detector](06-testing.md#testing-detector)): generic noun dump on every stage, Cell cells, `tools/nightly_detect.sh` | finds the unknown list (A) on every stage |
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

## Replicated fields: the first L2 piece (2026-11-03, fix/w40-scale)
<a id="sync-fields"></a>

The divergence detector (docs/06-testing.md#testing-detector) found one class in every stage: a mirror
keeps another size than its original (`Creature:scale:creature` 94 % of the Creature comparisons,
`Tribe:scale:citizen` 70 %, `Space:scale:ufo` 97 %). The fix is one stage-free mechanism, not a patch per
object: `mods/coop/fields.{h,cpp}` (wire codec `fields_wire.h`, unit test in tests/npc_tests.cpp).

* A module that mirrors a kind registers one row (`fields::add_kind`): the kind's id and name, its fix
  switch, its fields, `each` (the host's objects with their net ids), `mirror` (the guest's object of a
  host id, or none yet) and `active` (its stage). A field is `{id, name, read, apply, tolerance}`; a
  field without an `apply` is refused at registration, so "sent and never applied" cannot happen for
  a registered kind.
* Host, every 0.5 s: every field of every object; only the changed ones (beyond half the tolerance) and
  every value again every 4 s (`MSG_FIELDS` = 56, unreliable, chunked to one packet, through the
  world gate like the other world state). Guest: the newest value per (kind, id, field), forgotten 12 s
  after the host stops sending it; every 0.25 s each mirror is read from the game and set when it
  differs (a mirror made later, a game that changed it). The kind's switch acts on the apply only.
* `coop.fields` (counters per kind: sent, received, applied, reapplied: the game changed a value
  back, pending: no mirror yet, differ: an apply that did not take), `coop.fields list <kind>` (host:
  the game's values; guest: the mirror's value and the host's), `coop.fields scale <obj> [v]`.
* Kinds now: `npc` (creature NPC proxies, Creature and the Tribe stage's animals, `npc.scale`,
  BUG-0461), `tribe_member` (the members of every tribe, `tribe.scale`, BUG-0475). One field: `scale`
  (cSpatialObject GetScale vt 0x34 / SetScale vt 0x40, 2 %). Adding a field to a kind is one row.
* Not a field: a value the game recomputes every frame. UFOs (BUG-0479) are sized each frame by the
  game from the view and the ship's type (docs/re/space.md#space-ufo-scale): a size set from outside
  lasts one frame, so the partner's ship is fixed at the game's own setter (`space.puppet_scale`).
* Class G5 next to it (BUG-0487): a guest that gets NPC states for an id it has no proxy of asks the
  host for the spawn again (`MSG_NPC_RESYNC` = 57, `npc.resync`): the "resync of unknown ids" of L2 for
  the NPC kind.
* Not covered yet: Civ city citizens (BUG-0475's Civ half): each game simulates its own citizens
  (BUG-0476), there is no mirror to apply a size to; the size follows once citizens are mirrored.
  Done (2026-11-04, fix/w41-civ-citizens): Civ citizens are a moving kind of the world-object table (`citizen`,
  bound by position and walked to the host's, BUG-0476) and their size the fields kind `civ_citizen` (K = 4).
  A world-object kind can move now (`follow`: the host's place, its velocity, the follower's last goal; its own
  snapshot interval), docs/re/civ.md#civ-roads-citizens.

## Host-owned world state: one table of rows (2026-11-04, fix/w43-world-state)
<a id="sync-wsync"></a>

0.7.7 W1 (class BUG-0419 for what is not an object). `mods/coop/wsync.cpp` keeps one table of rows; a row says in
which stages it is in play, how the host's game is read (a small table of `{u32 key, up to 6 u32}`) and how a
guest's game is brought to the host's values through the game's own setters. The engine is stage-free:

* the host reads every active row every 0.25 s and sends the whole table (MSG_WSTATE 62, unreliable) once a second
  and at once when a row other than the sky changed; a newcomer gets it at once;
* a guest keeps the newest table per row (dropped after 6 s without one, and at the session's end) and every 0.5 s,
  while it plays the host's stage and world (the message gate drops MSG_WSTATE while it is elsewhere) and has its
  entity (not loading, not in an editor), compares its own game's values with the host's and applies the rows that
  differ. That is the re-alignment after every transition: the next check after a load, a rejoin, a death, an editor
  or the host's game over and reload applies the host's values again. `reapplied` counts applies after a match.
* `coop.wsync` the rows' state; `coop.wsync dump [row]` this game's own values (what tests compare: the guest's game,
  not the mod's table); `want <row>`, and test helpers `rel <pol> <delta>`, `goal <pol>`, `stage <i> <hex>`.

| Row | Stages | Read (host) | Apply (guest) | Switch |
|---|---|---|---|---|
| tod | Creature, Tribe, Civ | planet context orientation (`TimeOfDay_GetObject` +0x10) | SetOrientation + `TimeOfDay_Apply` (as MSG_WORLD_TIME did; that message is kept for older guests and ignored with the row on) | wsync.tod (BUG-0579) |
| day | Creature, Tribe, Civ | `cTimeOfDay::Get` +0x24 | `cTimeOfDay::SetDayDuration` | wsync.tod |
| stages | every planet stage | cPlayer +0x10FC..+0x110C (consequence traits) | field write (no setter; the History bar reads it) | wsync.stages (BUG-0577) |
| tribe_rel | Tribe | `CalculateRelationship(npc pol, player tribe pol)` per NPC village, with its position | `ApplyRelationship(npc, player, 0x530CF07, host - CalculateRelationshipAbsolute)` (the social success's raw-delta event: the tribe UI refreshes) | wsync.tribe_rel (BUG-0565) |
| tribe_goal | Tribe | Tribe Mission Manager +0x74: its village (pol, position), state +0x7C; cPlayer +0x10F0 / +0x10F4 | `TribeMission_SetTargetTribe` outside the frame (swaps the HUD card); the two floats written (the HUD bar reads them each frame) | wsync.tribe_goal (BUG-0564) |
| cell_spent | Cell | cCellSerializableData +0x2C | field write (the game's own writer is the editor result 0xE809A0) | wsync.cell_spent (BUG-0560) |

A village is matched by its political id (the guest loads the host's save, docs/re/tribe.md#tribe-detector-state),
else by the closest village within 40 m. Not moved onto it: the shared food and money (shared.cpp) are a ledger (the
guests' own spending is counted on the host), not host-only state; the Creature species relations (social.cpp
MSG_SPECIES, re-applied by enforce_table) and the Space empire relations (space_diplomacy.cpp) already re-apply
the host's table. Class test: `wsync_transitions` (tools/tests/coop_wsync_transitions.py: the host's game over and
reload, a rejoin, the guest's own changes; every row and the food compared from the guest's game).
