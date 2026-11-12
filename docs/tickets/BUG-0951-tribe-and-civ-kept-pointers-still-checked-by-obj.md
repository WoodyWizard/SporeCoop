---
id: BUG-0951
title: Tribe and Civ: kept pointers still checked by object_alive on their own memory (chiefs ring, epic, tools, vehicle and city tables): move them to game::LiveRef
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-12
source: fix/w48-freed (0.7.13 L1), code read
role: both
versions: 
fixed_in: unreleased
branch: fix/w48-freed2
commits: e0f359c9 c93007ff
tests: civ_mirror_lifetime, tribe_mirror_lifetime
signature:
docs: docs/re/tribe.md#tribe-mirror-other-users, docs/re/civ.md#civ-mirror-vehicle-reuse
related: BUG-0426, BUG-0844, BUG-0842
---

## Report
Found while fixing package L1 of 0.7.13 (fix/w48-freed); no player report. The L1 fixes moved the NPC proxies, the
nest herds, the Tribe mirrors, the deferred destroys, the effect and animation handlers to `game::LiveRef`
(the object list of the frame + the serial, docs/re/creature.md#creature-freed-block-reuse). About 60 other
`object_alive(<kept pointer>)` checks in tribe.cpp and civ.cpp (the ones BUG-0426 lists: tribe.cpp chiefs, the target
ring, the epic, held tools; civ.cpp vehicle and city tables) still read the kept object's own memory, which cannot
tell a freed block or a new object of the same type at that address (a creature's block comes back within 0.3-1.3 s,
a citizen's too).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Checked per place (fix/w48-freed2, code read and runtime). The 59 `object_alive` calls of tribe.cpp (40) and civ.cpp
(19) fall in six groups:

1. **A pointer read in this frame from the game's own lists** (a tribe's member vector, `all_tribes`, the vehicle and
   city lists, `for_each_object`, a tribe's tools): tribe.cpp `members` / `all_tribes` / tool and rack loops, the grab
   spots, the epic counters, the leader pick, the chiefs' placing (`g_chief` is rebuilt from the member vector in
   every pass); civ.cpp `all_vehicles`, the node / tribe / object searches, the shot targets. No pointer is kept
   across frames: `object_alive` only filters destroyed ones there. Nothing to change.
2. **The guest's Tribe mirrors** (`g_gm[id].c`): BUG-0844 fixed three users; the other nine (field replication,
   animation relay, held items, effects relay, the ring's member, the trackers, the sync-check) are BUG-0955, fixed
   with a FAIL / PASS.
3. **Tables rebuilt from the game's lists every cycle** (Tribe host `g_hm`, 0.12 s; Civ host `g_hv`): an entry whose
   object the game removed goes on the next cycle (it left the list at the removal, it is freed 10-40 frames later).
   A new object at a freed address before that cycle (the host paused in between) took the old id and owner: the
   entries keep the serial now and such an object is a new entry (`tribe.kept_refs`, `civ.mirror_lifetime`). Not
   reachable in a test without pausing the host's own update.
4. **Kept pointers asked in every frame** (the target ring in the HUD update, the epic in epic_tick, the babies the
   host's game made, the Civ guest's vehicle mirrors in prune_gone): the destroyed flag / the list is seen in the
   frame of the removal, before the free. They keep the serial now (`tribe.kept_refs`, `civ.mirror_lifetime`). The
   Civ case was tried at runtime: the game gives a removed mirror vehicle's block to a new vehicle on the 2nd try,
   and the guest did not drive it with the switch off either (prune_gone dropped the mirror first).
5. **Cities** (civ.cpp `g_city_owner`, `for_each_city`): a city is captured or changes owner, the game does not remove
   one inside a world, and the map is cleared at a world change (on_world_change): left as it is.
6. **Kept and kept exact by a hook**: `g_detached` (the citizen teardown hook erases it before the free). Test-only
   tables (`g_test_vel`, the console commands). Nothing to change.

Confidence: high for the groups (code read), runtime for group 2 (BUG-0955) and the Civ vehicles.

## Fix idea
Store a `game::LiveRef` (pointer, serial, noun) where a pointer is kept across frames and ask `game::alive` before any
call; a lint in make test against `object_alive` on a stored pointer (BUG-0426's "General fix"). One test per family
with `coop.npc reusehunt` / `coop.tribe reusehunt` (docs/06-testing.md#testing-freed-block-reuse).

## Fix
mods/coop/tribe.cpp: `tribe.kept_refs` (`kept_alive`: listed + serial) for the host's member table (a changed serial
is a new member, the bodies' branch asks it), the target ring, the epic, the babies. mods/coop/civ.cpp:
`civ.mirror_lifetime` (`gv_ok`: the serial bound with it; read from the vehicle, not the object list, since a vehicle
the mod just made can join the list later and prune_gone already drops removed ones) for every use of a guest's mirror vehicle (state, drive, the cycle's removals, shots,
trackers, `veh_mirror` / `local_vehicle`) and a serial in the host's vehicle table. Test hooks `coop.civ vehhold`,
`coop.civ reusehunt`. The lint against `object_alive` on stored pointers is not done: group 1 uses it legitimately
on fresh pointers and a lint cannot tell them apart.

## Verification
Test `civ_mirror_lifetime` (tools/tests/coop_civ_mirror_lifetime.py): PASS out/lab/20261112-050123; with
SPORE_FIX_OFF=civ.mirror_lifetime it passes too (out/lab/20261112-050242, 2 of 2: the reuse happened, prune_gone
dropped the mirror first). Tribe: tribe_mirror_lifetime 10/10 with tribe.kept_refs on (see BUG-0955).
no-fail-run: no reachable failure: every kept pointer of groups 3 and 4 is checked against the game's list in every
frame or cycle before the game frees its object (the Civ case tried at runtime); the changes are a refactor to the
shared mechanism, group 2's reachable failures are BUG-0955.

## History
- 2026-11-12: opened.
- 2026-11-12: fixed on fix/w48-freed2 (refactor per group; reachable failures in BUG-0955).
- 2026-11-12: play_civ on fix/w48-freed2 lost more guest orders once (31 %, out/lab/20261112-061048; 24 % in 20261112-062447, rel/0.7.13 18 % in 20261112-063357, 26 % on fix/w48-civ-speed before): gv_ok no longer asks the object list (a just-made mirror), see the next runs.
- 2026-11-12: after c93007ff: play_civ out/lab/20261112-064630-2 (guest orders lost 8 %, host 0 %), out/lab/20261112-064834 (11