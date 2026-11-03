---
id: BUG-0470
title: civ mirror vehicles (and other civ, tribe and space objects the mod creates on a guest) go into the guest's saves
status: open
kind: bug
stage: Civ
reported: 2026-11-02
source: code review of BUG-0469 (fix/w39-content-all review, item 2); class G9 of docs/generalisation-plan.md (BUG-0426)
role: guest
versions: 0.7.3.5
fixed_in:
branch:
commits:
tests: content_civ (steps 6-7: the guest saves, restarts without a session, loads the save; the mirror vehicles are in the loaded world)
signature:
docs: docs/re/civ.md
related: BUG-0426, BUG-0020, BUG-0469, BUG-0255
---

## Report
Not a player report: found in the code review of BUG-0469 (out/TASK2.md item 2), class G9 of
docs/generalisation-plan.md (BUG-0426). CLAUDE.md rule: "Our objects never go into a save: puppets,
proxies, mirrors and the herds made for them are not the player's world. Several crashes came from
saves that held them."

Observed (runtime, `tools/tests/coop_content_civ.py` steps 6-7, out/lab/20261102-213135): the guest
(Carol, lane B) saves its civilization world with `coop.save` (the game's own SaveGame) after the mod
created two mirror vehicles of the host's civilization; its game is then restarted without a co-op
session and loads that save, and `coop.civ` lists **2 vehicles**: the mirror vehicles of the session
were written into the guest's save and come back as ordinary vehicles of the loaded world.

Those objects are the mod's: the guest does not simulate the host's civilization, it only shows a
mirror of the host's vehicles (`civ.cpp guest_state`, `mirror_design`, `CreateVehicle`), of its
buildings (`build_in_city`) and its cities. They exist only while the co-op session runs: nothing
updates them after the load (the host's snapshots are gone), their designs come from the host's
world, and a later session on the same save can mirror the host's vehicles again on top of them.
The same gap is open for the Tribe (`SpawnNpcTribe` tribes and their members' mirrors) and Space
(UFO mirrors and puppets, the fallback puppet in the host's pack herd: modes.cpp:59-60), and for the
buildings the reconciled cities of a guest hold.

The mod's save filter (`mods/coop/savestrip.cpp`) only knows creatures and herds: it registers one
filter for `kCreatureNoun` / `kHerdNoun` (nest.cpp:734), so a civ vehicle, city, building or a tribe
never reaches it.

## Root cause
* Observed: `savestrip::add_filter` has one user (nest.cpp) and the filter in savestrip.cpp returns
  false for any noun that is not a creature or a herd; no civ / tribe / space object is looked at.
  The COM serializer writes every object of the world, so the mod's mirror vehicles (real cVehicles,
  added to `civ+0xB0` / `civ+0xD4` by `cCivilization::AddVehicle` 0xBF5750) are saved like any other.
* Observed (docs/re/civ.md, section 1 table): a vehicle lives in the civilization's
  `+0xB0` vector and its spatial object in `+0xD4` (the list the terrain cursor reads), and a city
  keeps its own list (`+0x120` combatant, the building slots). cCity / cCivilization read those
  vectors on load and in every update.
* Inferred from BUG-0020: a stripped object is written as a null reference;
  the noun list skips nulls, but other lists keep them (cGameBehaviorManager's update vector faulted
  at 0xB0EC8C until a null-reference hook dropped them). A civ vehicle or a city stripped without
  cleaning the lists that hold it would leave nulls where the city / civ code reads a pointer.

## Fix
Not fixed here: this is not the one-filter change the review allows without a verified save + reload
test. It needs
* a registry of the objects the mod creates per stage (mirror vehicles, cities/buildings the mod
  reconciled, tribe mirrors, UFO mirrors/puppets: the GUI identity is already in `g_gv` / `g_hv` /
  `g_mirror_of` / `g_hm`), and a savestrip filter for those nouns;
* a strip that leaves no null in `civ+0xB0`, `civ+0xD4`, the city's building slots and the civ's
  city list (read the three on load and in an update, BUG-0020's null rule), or a removal through
  the game's own RemoveVehicle / RemoveCity path;
* a save + reload test per stage (the civ half exists now: `content_civ` steps 6-7, which count the
  vehicles of the loaded save).

## Verification
no-fail-run: this ticket only records the issue; the reproducer is the PASS run of
`content_civ` in out/lab/20261102-213135 (the reloaded world lists 2 mirror vehicles, `saved files:
0 key(s) of the mod`). A fix must make those 2 vehicles absent from the reloaded world while the
session's own vehicles still sync (`coop_sync`), and its FAIL run is the same test with the strip
switched off.

## History
- 2026-11-02: opened (review of BUG-0469, item 2: the guest's save holds the session's mirror
  vehicles; reproducible with `content_civ`'s save and reload step).
