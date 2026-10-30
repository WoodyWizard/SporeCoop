# Host-centric simulation: what only lives around the host (audit, all stages)

Every Spore stage is a single-player game: it simulates, streams and culls the world around **its
own** player (the camera, the avatar, the player tribe or nation). In co-op the host's game is the
one world (chapter 9 rule 1), so whatever the host's game keeps only "around the player" exists
only around the host. A guest far from the host gets an empty or flickering world (Cell BUG-0174,
BUG-0279; Creature BUG-0280). The 0.8 Rivals mode (docs/11-rivals.md) puts every player at its own
base (Near / Middle / Far), so this class becomes the normal case there, not an edge case.

This file lists every place found (game and mod) that depends on the host's position, view,
avatar, progress or time, with the evidence, the severity for Together and for Rivals, and a
proposed fix. Marks as in the other `docs/re` files: ✔ seen in disassembly (convention / callers
checked) or at runtime, ~ inferred from decompilation or code reading, ? a guess to check.
Addresses are GOG 2024 (`dc04aee5`, the only build from 0.8 on) unless marked 2016.

Severity: **S1** the stage is broken or unfair for a far player, **S2** visibly wrong, a workaround
exists (stay near the host), **S3** minor or rare, **S4** not a problem.

## The activation spheres of the planet stages (2026-10-30, docs/w9-host-centric)
<a id="hostc-activation-spheres"></a>

Found in this audit (static, objdump on the GOG 2024 exe; not run). This is very likely the cull
behind BUG-0280, and it is one list that the mod can extend.

* ✔ **0xAD2300** `thiscall(this, uint32 modeId, a, b)` (callers not traced; `this` is a planet
  simulator object, ~ the same one whose `+0xA4` is the avatar, see 0xAC8C10..0xAC8CA0), every frame:
  1. reads the **camera position**: `IGameModeManager::Get` 0x67DCD0 -> vt 0x50 (~ GetViewer) ->
     vt 0x1C (~ the camera transform) -> 0x7C3E00(&pos, 0, 0, 0) (copies the transform's `+0x30`
     translation);
  2. appends **one** 20-byte sphere to the vector at `this+0x20` (begin) / `+0x24` (end) / `+0x28`
     (capacity), growth 0xACB5C0: `{Vector3 centre, float enterR2, float keepR2}` with
     enterR2 = 100.0² (`[0x1565D5C]`) and keepR2 = 250.0² (`[0x1565D60]`);
  3. by the mode: Creature 0x1654C01 -> **0xACE4E0**(a, b); Tribe 0x1654C02 -> 0xACE420(b),
     0xAD11E0(), 0xACE4E0(a, b); Civilization 0x1654C04 and the default branch (any other mode
     where 0x1020F30() returns 0, ~ Space on a planet) -> 0xACE420(b), `this+0xBC` 0xD582E0(b, 0),
     0xAD11E0(), 0xACF400(a, b);
  4. clears the vector (0x951B70 on `[begin, end)`, then size / 20).
* ✔ **0xAC8CC0** `thiscall(this, const Vector3* p) ret 4 -> bool`: true when `p` is outside the
  keep radius of **every** sphere of the vector. **0xAC8D30** (same signature): true when `p` is
  inside the enter radius of **any** sphere. Their only callers are in 0xACE4E0 (call xrefs: 0xACE69A,
  0xACE87B and 0xACE739, 0xACE74F).
* ✔ **0xACE4E0** (the activation pass; 2 stack args; reads the mode, `cGameNounManager` 0xB3D400):
  * objects of noun **0x36BE27E** (~ placed planet objects with a lazily made instance at `+0x1A4`):
    when inside an enter sphere their instance is created / restored (0xB93310(obj, -1, -1)),
    when outside every keep sphere it is saved back (`+0x1A8` position, `+0x1B4` orientation) and
    released (0xB22560 + Release): a streaming of objects around the camera;
  * creatures of noun **0x18EB45E** (cCreatureAnimal; skipped when the creature-game mode check
    0xB3D5D0()+0x2C is 1 or 2): a creature that is not destroyed, has `+0x135` and `+0xB67` set,
    passes 0xC0C240 (unless the creature-stage flag), is in the right table slot (`+0x24`, this+0xE0)
    and whose locomotion vt 0x58 is false, and is **outside every keep sphere** gets `vt 0xC8(1)`
    (~ back to the creature pool: herd 0, position (0 0 0), as observed in
    [creature.md](creature.md#creature-npc-pool-cull));
  * herds of noun **0x1BE418E** (cHerd): a herd whose brain range `[+0x120, +0x124]` does not hold
    the brain level `[0x169E370]` is disabled (`cHerd::SetEnabled` 0xC6AE80 (0), its nest
    `+0x120` = 1); an enabled herd whose centre (0xC6AE60, ~ the nest or home) is outside every
    keep sphere is disabled; a disabled herd inside an enter sphere is enabled (up to `[0x1565D04]`
    = 10 per pass); a herd also gets `+0x128` = "not within 400 m (`[0x13F0E64]`) of the point
    `[0x167EA30]`" (? that point, ~ the avatar or the camera).
* Only enabled herds run `Herd_Update` 0xC6D960 (members, spawning: docs/re/creature.md 10), so a
  disabled herd neither spawns nor ticks its members.
* Consequence: in the Creature stage the world lives in a 100 m (enter) / 250 m (keep) sphere
  around the **host's camera**; the partners' puppets add nothing. The observed 150-200 m
  (BUG-0280) is consistent: the camera is behind the avatar and a herd is judged by its nest.
* ~ The same pass runs in the Tribe stage and (through 0xACF400, not traced) in Civilization and the
  default branch: there the camera is the free RTS camera, so **what lives depends on where the
  host looks**, not where its tribe is.
* The fix point: after step 2 (or at the entry of 0xACE4E0 / 0xACF400) the host appends one sphere
  per partner view centre (same radii). Nothing else reads the vector, and the game rebuilds it
  every frame, so extra entries cannot leak. Proposed `.sym` lines (not added in this docs-only
  branch): `SporeCoop::PlanetSim_ActivationFrame=0x00AD2300`, `SporeCoop::PlanetSim_ActivationPass=0x00ACE4E0`,
  `SporeCoop::PlanetSim_OutsideAllKeep=0x00AC8CC0`, `SporeCoop::PlanetSim_InsideAnyEnter=0x00AC8D30`,
  `SporeCoop::Herd_Centre=0x00C6AE60` (all `likely`; GOG 2016 addresses not looked up).

## Inventory (2026-10-30, docs/w9-host-centric)
<a id="hostc-inventory"></a>

### Game side

| # | Stage | Where | What a far player gets | Evidence | Together | Rivals |
|---|---|---|---|---|---|---|
| G1 | Creature | activation pass 0xAD2300 / 0xACE4E0 ([above](#hostc-activation-spheres)) | NPCs pooled, herds disabled, nests respawn a member every 3-6 s that is pooled again (flicker) | ✔ runtime symptom (BUG-0280, lane 4), ✔ static mechanism (this audit) | S2 | S1 |
| G2 | Creature | same pass: herd brain range `[+0x120,+0x124]` vs `[0x169E370]` | nests of the "wrong" brain range are off near everybody; the range is the **host's** brain level | ✔ static | S4 (shared level) | S2 (own brain level per player) |
| G3 | Creature | same pass: herd `+0x128` from `[0x167EA30]` within 400 m | unknown effect of the flag | ~ static | S3 ? | S3 ? |
| G4 | Creature | `NPCTickAI` 0xC09570 notices only `cGameNounManager+0x54` (docs/re/creature.md 14.2) | without the mod's swap NPCs ignore guests; with it, only NPCs within 80 m of a partner and nearer to it than to the host | ✔ | S4 (swap works) | S2 (hostility must be per player, 11.4) |
| G5 | Cell | `PopulateTiles` 0xE7D370 around the view `G+0x4128`; cull 0xE5CDE0 against the box `0x16B3C88`; 0xE78C00 | no food and no NPCs from 23-45 m from the host on | ✔ runtime (fix/w7-eat), ✔ static ([cell.md](cell.md#cell-new-players-food)) | S1 (BUG-0279: new players) | S1 |
| G6 | Cell | 0xE78C00 drops the graphics (0xE66010(0)) of cells outside the view; the soft body runs only with GFX `+0x248` (docs/re/cell.md 6) | once cells are kept near a guest (G5 fix) they move without their soft body on the host: mirrors may face or drift differently | ~ static, not seen | S2 after G5 | S2 after G5 |
| G7 | Cell | the pool's level: GetScaleDifference 0xE57400 reads the avatar (-1) as the **host's** food level; PopulateTiles takes the level | Rivals with own bars: a guest at another level swims in a world spawned for the host's level | ✔ static (docs/re/cell.md 9) | S4 (shared bar) | S1 (design decision needed) |
| G8 | Cell | ShouldNotAttack 0xE57480 tests only `G+0x411C` | handled: the mod swaps it for a puppet victim | ✔ fixed 2026-10-23 | S4 | S4 |
| G9 | Cell | SimUpdate events 0xE7E7A0 | not known whether timed spawns (predators, the mate) are placed around the view | ? | S3 ? | S3 ? |
| G10 | Tribe | the activation pass, mode 0x1654C02, plus 0xACE420 / 0xAD11E0 (not traced) | the animals the members hunt live around the **host's camera**: a host looking elsewhere pools the animals near the guests' members | ~ static | S2 | S1 (own tribe at the base distance) |
| G11 | Tribe | citizens of all tribes (`cCreatureCitizen`, noun 0x18EB4B7) and their BT | not culled by the creature loop (another noun) ~; whether 0xACE420 / 0xAD11E0 stream citizens, huts or tribe tools is open | ? | S3 ? | S2 ? |
| G12 | Civ | vehicles: strategy 0xDCEDB0 ticks `GetAllVehicles` (docs/re/civ.md 5); cities: `cCitySimulator` 0xAE6190 for the whole planet | nothing found that depends on the view; 0xACF400 / 0xACE420 / 0xD582E0 (the civ branch of the pass) not traced | ✔ (vehicles, cities), ? (the civ branch) | S3 ? | S3 ? |
| G13 | Civ | ShouldNextCityAppear 0xAE2EC0: the **host's** player flag (spice claim / vehicles) and net worth or income | NPC cities appear at the pace of the host's nation | ✔ static + runtime (docs/re/civ.md) | S4 (shared money) | S2 (NPC cities are the race's goal) |
| G14 | Space | each player flies its own galaxy copy; in the host's place the host's NPC ships are the real ones | the guest's own game events there (a raid on its colony in that system, its mission ships, its colony defenders) are destroyed by squadron (G-side: the events are the guest's own; M10) | ~ code reading | S3 | S2 (own empires, rivals can share a system) |
| G15 | Space (planet) | high-LOD planet `cSimulatorSpaceGame+0x54`; the activation pass default branch on a planet | planet fauna and placed objects stream around each game's own camera (local, not synced: fine); host's NPC ship traffic on a planet: not known if spawned around the host's ship | ? | S3 ? | S3 ? |
| G16 | Creature / Tribe | terrain and collision LOD around the camera (docs/re/graphics.md: the game puts a creature below the terrain back on it) | NPCs kept alive far from the host's camera (after G1) may stand on a coarse height: wrong heights on the guests | ? | S3 ? | S3 ? |
| G17 | Creature / Tribe | animation and effect LOD far from the camera | attacks timed by animation events may not fire on the host for NPCs far from its camera | ? | S3 ? | S2 ? |
| G18 | all | time: the host's pause menu, captions, cinematics and its editor stop the world for everybody (docs/re/creature.md 7, BUG-0151, BUG-0205, BUG-0233) | a far, independent player waits for the host | ✔ | S3 (workarounds: host away) | S1 (a race; every player evolves at its own time) |
| G19 | all | "the player" identity: `G+0x411C`, `cGameNounManager+0x54`, `GetPlayerTribe`, the player civ and empire | only the host's faction is "the player" on the host (Rivals R1/R2/R4) | ✔ | S4 | S1 (docs/11-rivals.md 11.9) |

### Mod side

| # | Where | Rule | Far player | Together | Rivals |
|---|---|---|---|---|---|
| M1 | `cellsync.cpp:1102` host_tick | cells within **120 m of the host's avatar**, one list to every guest | nothing near a far guest is sent even if the host's pool had it | S1 (with G5) | S1 |
| M2 | `cellsync.cpp` ~902 and the PopulateMarker hook | the guest's own population is off everywhere; local gameplay cells are purged | blocks a guest-local fallback (by design today) | S4 | design |
| M3 | cellsync | no host-away state (BUG-0205 Cell part): mirrors stay AI-locked while the host is in the menu, an editor or a load | frozen pool | S2 | S1 (G18) |
| M4 | `npc.cpp:32-33, 363-393` | NPC proxies within 110 m (+20 m hysteresis) of **each** guest: already per guest | correct, but the host's game has nothing there (G1) | S4 | S4 |
| M5 | `npc.cpp:36, 370-372` | Tribe stage: NPC creatures within **250 m of the player tribe** for every guest (`tribe::center`) | animals farther than 250 m from the host's village never reach any guest | S2 | S1 |
| M6 | `npc.cpp` update_filter (npc_stage includes Tribe) | the guest's own NPC creatures are removed in Creature and Tribe (kept only while the host is away, `g_host_away`) | with M5 and G10: no animals at all outside the host's area | S2 | S1 |
| M7 | `npc.cpp:154-189` ai_target | an NPC treats the nearest partner within 80 m (3 m hysteresis) as the player | fine where NPCs exist | S4 | S2 (per-player hostility) |
| M8 | `npc.cpp:240-330` retire / retire_pooled (BUG-0274) | a pooled or freed host NPC is despawned at once | follows G1 faithfully (the flicker is the game's) | S4 | S4 |
| M9 | `tribe.cpp:405-491` host_tick | all members of up to 32 tribes, no range | fine; bandwidth grows with the planet, not with distance | S4 | S4 |
| M10 | `squadron.cpp:201-262, 402-421` | all NPC ships of the host's scene to every guest in the same place; the guest destroys its own NPC ships there (keeps puppets, retired mission ships, the posse type 3) | the guest's own raids / mission ships in the host's place vanish (G14) | S3 | S2 |
| M11 | PlayerState (07-modes 7.1) | Tribe / Civ: the guest's position is its **mouse pointer**; the host does not know where the guest's camera is | needed for per-player view centres in Tribe / Civ | - | - |
| M12 | `tools/tests/coop_multi.py:225-227` measure | NPCs count for coverage when within 90 m of the guest's own key position | in Tribe the host sends NPCs around the tribe (M5), not around the guest's pointer: a guest pointing away from the village is "missing" NPCs, a possible cause of BUG-0288 (~, not checked) | S3 (test) | - |
| M13 | civ.cpp alarm boost (600 m), roles (200 m), tribe chief (15 m), cell bond (6 m) | design ranges between players | not host-centric | S4 | S4 |

## Designs (2026-10-30, docs/w9-host-centric)
<a id="hostc-designs"></a>

### A. Host-side view centres (one list for every system)

A host-only module (`views.h`): per player `{id, stage, place, centre, radius, age}`, updated from
the player states (Cell / Creature: the avatar; Tribe / Civ: the camera target and the positions of
the player's own members or vehicles, M11 needs a new field or message; Space: the place). Every
host system reads it:

* Creature, Tribe (and Civ / Space planets if 0xACF400 does the same): hook 0xAD2300 after the
  `push_back` (or the entry of 0xACE4E0 / 0xACF400) and append one sphere per partner centre. The
  game then keeps herds enabled and NPCs out of the pool around every player (G1, G10). Cost: up
  to 4x the active creatures on the host (CPU, the creature pool's size, the nests' spawning).
* Cell: a second tile set per partner (PopulateTiles 0xE7D370 with an area around the partner's
  cell; the tile list layout is still open, docs/re/cell.md#cell-new-players-food), the cull
  0xE5CDE0 hooked to test every partner's box, cells near a partner kept with `+0x354`. Plus G6:
  give kept cells their GFX (or accept a stiffer motion). Watch the pool cap (~420 cells).
* The mod's interest filters (M1, M5, and M12 in the tests) send each guest the objects near its
  own centre (npc.cpp already does this per guest; cellsync and the tribe NPCs do not).
* Risks: the host's frame time (two to four simulated areas), bandwidth (each guest gets its own
  area), pool limits (cells, creatures, herds: the pass enables at most 10 herds per frame), the
  sphere's centre is a camera for the host but a body position for partners (pick radii so a
  partner's screen is covered: the Creature camera sits a few metres behind and above the avatar),
  and ordering with the native "only around the player" assumptions (spawning, migrations,
  epics). Good: no ownership change, every object stays host-authoritative, saves unchanged.

### B. Guest-local simulation where the host has nothing

A guest that has no host objects around it (no host centre within the keep radius) runs its own
game there, as it already does while the host is away (`g_host_away` in npc.cpp; BUG-0151 for
Tribe / Civ): its own NPCs, its own food, marked local-only. When a host centre comes near, the
local objects of that area are removed before the host's mirrors arrive (or adopted, see C).

* Good: no extra load on the host, scales with players, the guest's game is a full single-player
  world.
* Risks: the two games disagree about the far area (a herd killed locally lives on the host; an
  extinction, a species relation, food eaten locally must be reported to the host and counted
  once); handoff glitches (things vanish when the host comes near); Cell food must still count into
  the shared or per-player totals; saves are the host's, so the far area is not saved.

### C. Region authority (hybrid)

The host owns the area around its centre (and around any guest within it), each far guest owns
its own area; objects crossing between areas change owner with their state (the NPC ids of npc.cpp
and the cell indices already give network identity). This is the "right" design for Rivals but
needs ownership transfer for herds, nests and cells, which the game does not support natively.

### Recommendation

* **Creature, Tribe**: A (the sphere hook) first: one hook, a list the game already supports, no
  ownership change. Measure the host's frame time with 4 players apart. Keep B as the fallback
  when A costs too much (the guest-local code exists for the host-away case).
* **Cell**: A for food and NPCs (BUG-0174 plan 1); B (food only) if the tile list cannot be
  doubled. Rivals Cell also needs G7 decided first: one shared pool level (bars per player but the
  world follows the leader, or the lowest) or Cell rivals only for players at the same level.
* **Civilization**: nothing to do unless the civ branch (0xACF400) turns out to stream something.
  G13 (NPC cities follow the host's progress) is a Rivals rule question: drive `AppearNextCity`
  from the host by the best (or the sum) of the players' progress.
* **Space**: keep the per-galaxy design; M10: do not destroy the guest's own event ships in the
  host's place (keep ships whose owner mission or raid belongs to the guest's game), or hand
  those events to the host.
* **Time (G18)**: Rivals needs a rule for pauses (the host's pause menu does not pause the world
  in a session; captions and cinematics are skipped or local only), and per-player editors must not
  stop the host's world (the guest-editor path exists for Together's own editor).

## Order of work and what to measure first (2026-10-30, docs/w9-host-centric)
<a id="hostc-order"></a>

Prioritized (Together impact first, then what Rivals cannot start without):

1. **Cell G5 + M1** (BUG-0174, BUG-0279): S1 in Together already (new players). The view-centre
   list and a per-guest cell send; the tile set per partner. Running now on fix/w9-cell-food.
2. **Creature G1** (BUG-0280): the sphere hook on 0xAD2300 (host only), npc.cpp already sends per
   guest. Running now on fix/w9-creature; the hook point above should shorten it.
3. **Tribe G10 + M5 + M6 + M11**: the same hook in mode 0x1654C02 with each guest's centre; the
   tribe NPC relevance per guest (camera and own members); the guest's camera in its state. Also
   look at BUG-0288 through M12.
4. **G18 time rules** and **G19 perspective** (R1): blockers for Rivals in every stage, not
   spatial but host-centric in the same way.
5. **G2 / G13 / G7** (progress that is the host's: brain range, NPC city pace, cell level):
   Rivals rule decisions plus small hooks.
6. **G6** with the Cell fix, **M10** for Space, **G3, G9, G11, G12, G15, G16, G17** when the runtime
   checks below say they matter.

Runtime checks needed (none done in this audit, all lanes were busy):

* R-H1 (Creature): break on 0xAD2300 / 0xACE4E0: confirm the call chain, the 100 / 250 m radii,
  the camera as centre (`coop.npc creatures` distances while the camera is turned and zoomed), and
  that one appended sphere at a guest's puppet keeps its nest (`coop_npc_pooled.py` must pass).
  Watch the host's frame time with 3 extra spheres.
* R-H2 (Creature): what `vt 0xC8(1)` is on cCreatureAnimal (pool, hide, sleep) and what `[0x167EA30]`
  and herd `+0x128` are.
* R-H3 (Tribe): with the host's camera panned 400 m away from the village, do the animals near the
  guests' members get pooled (`coop.tribe objs 18EB45E`)? Trace 0xACE420 and 0xAD11E0 (streaming
  of citizens, huts, trees or tools?).
* R-H4 (Civ / Space planet): trace 0xACF400 and 0xD582E0 (`this+0xBC`); check whether NPC ship
  traffic on a planet spawns around the host's ship (two ships 300 m apart on one planet,
  `coop.fleet list` on both).
* R-H5 (Cell): the tile list layout at `+0x1018` (one more list per partner), and whether a
  gameplay cell without GFX (G6) still moves and steers like one with GFX.
* R-H6 (Cell): does SimUpdate's event step 0xE7E7A0 place spawns relative to the view (G9)?
* R-H7 (all): the host's frame time and the bandwidth per guest with 4 players at Far bases
  (Rivals), once A is in for Creature and Tribe.
