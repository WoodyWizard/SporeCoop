---
id: BUG-0469
title: content check for every model kind: Civ vehicles, buildings and city designs, Space NPC ship parts, cell and UFO paints (rest of BUG-0424 after BUG-0452)
status: fixed
kind: crash
stage: All
reported: 2026-11-02
source: BUG-0424 sibling list, owner request 2026-11-02 (0.7.3.6)
role: guest
versions: 0.7.3.5
fixed_in: 0.7.3.6
branch: fix/w39-content-all
commits: 2a99049e, 7d95b829, 4cfa199a, cf065107, 9130fa96, 958b870c, e328a099, a63959db, d4859b3c, 5d1ef97b, 8dc2cf80
tests: content_civ (tools/tests/coop_content_civ.py), content_space (tools/tests/coop_content_space.py)
signature: 3010a413:SporeApp.exe+0x46C82A read 0x00000000 (dc04aee5 0x46C82A, FUN_0046C4D0: the vehicle/UFO model bake, a part's property object null, prop 0xAFFF3A14)
docs: docs/re/creations.md#creations-content-all-kinds, docs/re/civ.md#civ-content-all-kinds, docs/re/space.md#space-mirror-content
related: BUG-0424, BUG-0452, BUG-0323
switch: content.all_kinds, content.model_key
---

## Report
Not a player report: the rest of BUG-0424 (class G7 of docs/generalisation-plan.md), asked for 0.7.3.6.
0.7.3.5 checks the part and paint ids of every *received creation* (creature, cell, UFO) and the key of a
host ship's model, but not the other models the mod makes a game build: a civ mirror vehicle (built from
the guest's copy of the civilization's vehicle design, `CreateVehicle` with `GetDesignKey`), a building of
a city on a guest (`SetBuildingModel` with the civ's building design), and the *parts inside* a host NPC
ship's model in Space (BUG-0323 checks the key only). A design or model whose parts come from a pack, a mod
or another profile's Sporepedia is baked by a game that does not have them and faults (the class of
BUG-0452: a null property list in the model bake).

## Root cause
Observed (Ghidra, lanes 4/5-6, `tools/tests/coop_content_civ.py`, `coop_content_space.py`):
* A model whose part this game does not have faults in its model bake, at a place that depends on the kind:
  the creature bake at 0x00441F3B (BUG-0452), the vehicle and UFO bake at 0x0046C82A (`mov eax,[edx]`, edx
  the part's property object, 0xC0000005 read 0; `SporeCoop::ModelBake_PartProp_Null`).
* Civ: the guest builds a mirror vehicle from its own copy of the civilization's design
  (`GetDesignKey(VehicleDesignId(purpose, loco))` -> `CreateVehicle`); that design's model is what the
  guest's game bakes, and nothing checked it. A vehicle whose design model names a part the guest lacks
  faults 456 times in one run with `content.all_kinds` off (FAIL run out/lab/20261102-202807).
* Civ buildings: `cCity::SetBuildingModel` (0xBDB940) picks the design id by building type
  (0xFF10521/0x1A55E4D/0xECADE42 -> 0x4E3F7777/0x72C49181/0x47C10953) and reads the civ's design key
  itself; when its key instance is 0 it generates the design (FUN_00bf9440). The guest's own building
  designs come from the shared world's save, so they can name ids the guest lacks.
* Space: the ship snapshot carries the host ship's model key and the guest spawns the mirror with it;
  BUG-0323 checks only whether the *key* exists. A key the guest's game has whose model names a part it
  does not have faulted the same way (0x46C82A, not 0xE98DD0).
* The three paint effect dwords at byte 26 of a bem header are the creature ones for vehicle, UFO and
  building models too (they name base game skin paints); a cell model names ids no package holds as an
  effect record (cell paints are defined elsewhere), so cell paints cannot be checked.
Inferred: in a real session the vehicle, building and ship models that reach a guest with unknown ids come
from the host's world save and Sporepedia cache (designs of another profile, pack or mod parts); the
fixtures here use a base game model with one part id of `Spore_Pack_03` (that pack has creature parts only,
so the id is one no game has for a ship or vehicle model: the "another profile's modded content" case).

## Fix
`content.all_kinds` (on by default; `coop.fix content.all_kinds off` = 0.7.3.5, the new call sites pass the
key as they get it).
* `creations::ensure_model(model, ModelKind, allow_copy)` (mods/coop/creations.cpp): this game has the key
  AND every part / paint id the model names (`model_part_refs`, `part_known`, `paint_known`). An id this
  game lacks is shown as a substitute in a copy of the model's records (the `content_packs.h` tables),
  registered at once **under the model's own key** in a package at priority 3000; a model with an id that
  has no substitute (or whose copy cannot be written or cannot be shadowed) is refused. Each unknown id is
  logged once per session (`creations: content: <kind> model <id> part <id> unknown here: ...`). Kinds:
  creature, cell, UFO, vehicle (vcl 0x24682294 / 0x40646200, parts 0x40646000), building (bld 0x2399BE55 /
  0x40636200, parts 0x40636000). Paints are checked for creature, vehicle, UFO and building models; not for
  cell models (their header is not the creature one).
* Review fix (2026-11-02, item 1 of out/TASK2.md): the copy used to be registered under an instance of the
  mod's own (0x7D......), and a civ mirror vehicle was created from it. Mirror vehicles are not kept out
  of saves (BUG-0470), so the guest's save named a model key no package answers after a restart (or
  without the mod), which is the very bake fault this ticket fixes. The copy now shadows the model's own
  key: package at priority 3000 (above the game's packages, the host's world at 2000 and My Creations at
  1000), the resource manager walks that list highest first and probes `OpenRecord` (no global key index,
  docs/re/creations.md §3), and `ensure_model` verifies after registering that the key answers from our
  package (`FindRecord` 0x8DFC80 + the database's `GetLocation`), refusing otherwise: no key of the mod's
  reaches the game or a save. `coop.fix content.model_key off` = the first version (a key of ours), for
  the lab.
* `civ.cpp`: a mirror vehicle is built with the checked key; a design this game cannot build at all falls
  back to a zero key, which the game fills with its own design of the same purpose and locomotion
  (`Simulator::cVehicle::Load` 0xCA67C0, 0xCA68E9; logged once per design). A building's design cannot be
  replaced (SetBuildingModel reads the key itself): on a guest a design with ids this game lacks is left
  out and logged (`coop.civ design <city> [house|factory|fun]` shows the key and the verdict).
* `squadron.cpp`: a host ship's model key and the parts / paints inside it; a substituted copy, else the
  local model of a ship of that empire as BUG-0323. `coop.fleet fakemodel 2 <instance>` (test) sends that
  model instance.
* `content_packs.h`: `kBasePartFallbacks` entries for vehicle (same ids as UFO: the same part group) and
  building parts (the base game's 224 building parts have no 0xFE byte).

## Verification
* FAIL (`SPORE_FIX_OFF=content.all_kinds`): `out/lab/20261102-202807` (lane 4 + guest lane 5, 2 repeats).
  4/4 FAIL: the civ guest faults 456 and 467 times at 0x0046C82A (data 0), the space guest 2 times at the
  same address; no unknown-id log line, `coop.creations content` = `checks=0` (the switch off).
* PASS (fix on): `out/lab/20261102-202217` (lane 4 + guest lane 5, 2 repeats). 4/4 OK: the civ guest logs
  `creations: content: vehicle model 29030001 part 75A5D2D4 unknown here`, builds the mirror from a
  substitute copy (`checks=2 same=1 substituted=1 refused=0`, `mirror_designs=1`, a new mirror created)
  and the host's house from the civ's own building design (`model ok`, buildings 1 -> 2); the space guest
  logs `creations: content: ufo model 29030002 part 75A5D2D4 unknown here`, builds the mirror ships from a
  copy (`substituted=1`, `content_checked=2`, `unknown_models=0`) and shows them.
* `coop.fleet spawn` sends the test model to both guests; the unpatched BUG-0323 test
  (`space_mirror_model`) and the civ tests are unchanged.
* Review fix (item 1), civ: PASS (fix on, `content_civ`, new save/reload step) `out/lab/20261102-213135`:
  the guest saves after the mirror was built, its game is restarted without a co-op session and loads the
  save: 2 saved vehicles load with `total faults: 0`, the save slot names the design's own key 0x29030001
  and the old scheme's instance 0x7D72C793 is absent. FAIL (`SPORE_FIX_OFF=content.model_key`, the first
  version): `out/lab/20261102-213354`, the save names 0x7D72C793 and not 0x29030001 (2 checks fail).
* The lab gives no free lane when the run lanes plus `--skip 1,2,3,7,8,9,10` cover lanes 2..9 (both tests
  are `uses_free_lane`, the guest needs a lane without the pack), so the two runs above were made with
  `--lane 4` (free lane 5); the same tests on `--lane 4 --lane 5 --lane 6` SKIP (`out/lab/20261102-185757`).
* Regression: `out/lab/20261102-191952` (`smoke,unknown_parts,guest_missing_pack,guest_pack_paints,creations,
  civ_coop,space_coop,play_civ` on lanes 4/5/6): 15 ok, 2 skipped (`unknown_parts`, `guest_missing_pack`:
  no free lane with those flags), `unit` and `play_civ` failed in that parallel run and pass alone:
  `unit` (`out/lab/20261102-194951`, make test through the lab: the lab selftest's interrupt cases are
  timing sensitive under load; `make test` run directly passes with 228 checks) and `play_civ`
  (`out/lab/20261102-195412`, a bot's right-click move did not happen: a flake of the bot).
  `unknown_parts` and `guest_missing_pack` pass on `--lane 4` with the free lane:
  `out/lab/20261102-193721` (both ok).
* `make -j8` and `make test` pass.

## History
- 2026-11-02: opened (rest of BUG-0424, class G7).
- 2026-11-02 (fix/w39-content-all): `creations::ensure_model` for every model kind behind
  `content.all_kinds`; civ mirror vehicles and buildings, space host ship models; the vehicle/UFO bake
  failed at 0x0046C82A with the switch off; tests `coop_content_civ.py` / `coop_content_space.py` (FAIL and
  PASS runs above); docs/re/creations.md#creations-content-all-kinds, docs/re/civ.md#civ-content-all-kinds,
  docs/re/space.md#space-mirror-content.
- 2026-11-02 (fix/w39-content-all, review of out/TASK2.md): the substitute copy goes under the model's own
  key at priority 3000 (never a 0x7D key of the mod's), verified with FindRecord after registering and
  refused otherwise; switch `content.model_key`; the civ test now saves, restarts without a session and
  loads the save (PASS out/lab/20261102-213135, FAIL with `content.model_key` off out/lab/20261102-213354);
  BUG-0470 opened for the mirror vehicles that stay in the guest's save.
- 2026-11-02: released in 0.7.3.6.
