---
id: BUG-0059
title: vehicle and city tables survive a load, so new vehicles can inherit old owners and guests can use freed cities
status: fixed
kind: crash
stage: Civ
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#37
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-tribe-civ
commits: ba9dc46
tests: civ_reload, civ_play, civ_coop
signature:
docs: docs/re/civ.md#civ-pointer-tables
related: #37
switch: civ.world
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/37 (full text, discussion).

Tribe resets its tables when the world changes; Civilization has no `on_world_change` at all. After the host loads another civ save during a session, the host keeps the old vehicle table, and the guest keeps the old host-city table (raw `cCity*` pointers). Results:

- A vehicle of the new world created at a reused address can inherit an old id and a player owner, including an NPC nation's vehicle.
- A guest can build into, reconcile or beacon a freed city until a full city list arrives.

Severity: wrong behaviour, possible crash on the guest.

### Where
- coop.cpp calls only `npc::on_world_change` and `tribe::on_world_change`: [coop.cpp#L1139-L1148](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1139-L1148)
- Host: stale `g_hv` entries are kept when the address is alive again: [civ.cpp#L961-L988](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L961-L988)
- Host command check trusts that owner: [civ.cpp#L1165-L1170](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1165-L1170)
- Guest: `g_host_cities` is resized but only the entries in the received chunk are refreshed: [civ.cpp#L1359-L1377](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1359-L1377)
- Guest users of `g_host_cities[ci]`:
  - `reconcile_buildings` via the buildings part of a snapshot: [civ.cpp#L1420-L1438](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1420-L1438)
  - `guest_build`: [civ.cpp#L1578-L1584](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1578-L1584)
  - the alarm beacon: [civ.cpp#L1626-L1630](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1626-L1630)
- Guest: `g_city_owner` entries are only erased for cities that a host city maps to; others stay forever and are read every frame for the tags: [civ.cpp#L1644-L1650](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1644-L1650)
- `on_session_end` is the only reset and runs only on a disconnect: [civ.cpp#L1633-L1642](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1633-L1642)

### How to reproduce
1. Civilization stage, host + 1 guest, both with several vehicles on the planet (fixture `civ_npc`, a few `coop.civ buy`).
2. The host loads the same or another civ save while connected; the guest follows.
3. Host: `coop.civ` and compare the `veh #id owner=` list with the game's political ids (`pol=`). A vehicle with an NPC `pol` but a player owner is the bug. It needs address reuse, so repeat the load a few times.
4. Guest crash variant: during the guest's first seconds in the new world, drop snapshot chunk 0 (lab packet loss, `coop.civ chunk 200` on the host to get many chunks) and let a city's buildings chunk through.

### Expected
A world change clears every civ table that holds game pointers on both machines. Ownership in the new world is assigned fresh.

## Root cause
**What happens**

Host:
1. The load frees all vehicles of the old world. `g_hv` still maps those addresses to `{id, owner}`.
2. In the first `host_tick` of the new world, the prune keeps every entry whose address is in the new vehicle list (`alive.count(it->first)`). Vehicles have one size and come from the same heap, so the new world's vehicles often land on old addresses.
3. Such a vehicle keeps the old id and owner, because `if (g_hv.count(v)) continue;` skips the owner assignment and the `pol_of(v) == ppol` check. An NPC nation's vehicle can come out as "player 1's". `own_vehicle` then accepts that guest's orders for it, and the guest sees its name tag.

Guest:
1. After its own load, the guest's `g_host_cities` holds pointers to the old world's cities. They are only replaced for the indices in a received cities chunk.
2. A snapshot is split into chunks (cities first, then spice nodes, one city's buildings, vehicles). If the cities chunk is lost (unreliable) while the buildings chunk arrives, `reconcile_buildings(g_host_cities[ci], ...)` reads the building vector of a freed city. It may also call `DestroyBuilding` / `build_in_city` on it.
3. The same goes for a reliable `MSG_CIV_BUILD` or `MSG_CIV_ALARM` that arrives before the first cities chunk.

### Confidence
- Missing reset and code path: CONFIRMED by reading.
- Wrong owner after a load: PLAUSIBLE, since it depends on heap address reuse.
- Guest use of a freed city: PLAUSIBLE, since it needs a lost cities chunk or an early reliable message in the first cycles after the load.
- Not run on the lab.

## Fix
Fixed on fix/w1-tribe-civ (ba9dc46):

- `civ::on_world_change()`: clears the co-op tables (see BUG-0040), runs `on_session_end()` (host
  vehicle table, guest mirrors, city owners, host-city table) and resets `g_rx_cycle`,
  `g_cycle_ids`, `g_cycle_chunks` and the per-frame vehicle cache. coop.cpp calls it next to
  `tribe::on_world_change()`. It is skipped in the vehicle / building editors (`kEdit`, `kEditor`),
  which also have no co-op adapter: an editor visit must not lose the owners and gifts.
- Host: `HV` keeps the vehicle's political id; an entry whose `pol_of(v)` changed is listed anew
  (new id, owner from the nearest player city).
- `coop.civ fix world 0` restores the old behaviour (tests).

## Verification
`tools/tests/coop_civ_reload.py` (suite `civ_reload`), see BUG-0040. In the game the reload of the
same save runs `on_world_change` once (`world_resets=1`, the host is back in Civ in ~7 s).

- FAIL with `--nofix` (out/lab/nofix-20261027/nofix_civ_reload.log): `world_resets=0`, the old world's
  `alarm_at` entry is still there after the load.
- PASS with the fix: lab runs 20261027-184454, 20261027-185915 (2x). The test also asserts that no
  vehicle keeps its old address and id and that no vehicle of another nation has a player owner.

Not observed: address reuse (4 of 4 vehicles got new addresses in the one reload looked at), so the
inherited owner was never seen; the guest-side freed-city use (a lost cities chunk) was not forced.
The guest's tables are cleared by the same `on_world_change` when the guest loads a world.

### Related
- The city-alarm boost writes into freed vehicles for the same reason (separate issue).
- Tribe has `on_world_change`, but it misses some tables too (separate issue).
- docs/re/civ.md, section "8. Implementation recipe (co-op "governors")".

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #37.
- 2026-10-27: ticket opened from the issue.
- 2026-10-27: fixed on fix/w1-tribe-civ (ba9dc46), test civ_reload (FAIL with --nofix, PASS with the fix).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: civ.world (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
