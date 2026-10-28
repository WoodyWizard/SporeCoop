---
id: BUG-0040
title: the city-alarm speed boost can write into freed vehicles after a leave or a load
status: fixed
kind: crash
stage: Civ
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#18
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-tribe-civ
commits: ba9dc46
tests: civ_reload, civ_play, civ_coop
signature:
docs: docs/re/civ.md#civ-pointer-tables
related: #18
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/18 (full text, discussion).

The host's "defenders rush to the attacked city" boost keeps raw vehicle pointers in `g_boost`, and nothing ever clears that map. Each frame `boost_tick` writes the vehicle's speed field `+0x218`. The only checks are a lookup in our own map and `object_alive`, and `object_alive` cannot tell a freed vehicle. After a `coop.leave` or a load during a boost, the next civ session writes a float into freed heap memory. Severity: crash / heap corruption (narrow trigger).

### Where
- `boost_tick`: [civ.cpp#L659-L678](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L659-L678)
- Where boosts are created: [civ.cpp#L647-L656](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L647-L656)
- `boost_tick` runs before the `g_hv` prune in `host_tick`: [civ.cpp#L945-L970](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L945-L970)
- `on_session_end` does not clear `g_boost`, `g_siege`, `g_alarm_at`, `g_joint_told` or `g_city_gifted`: [civ.cpp#L1633-L1642](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1633-L1642)
- `do_leave` calls no `civ::on_session_end` at all: [coop.cpp#L1761-L1771](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1761-L1771)
- `object_alive` only reads the object's own destroyed flag: [game.cpp#L139](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L139)
- The code already knows this problem for guests (`exists()`): [civ.cpp#L76-L92](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L76-L92)

### How to reproduce
1. Civilization stage, host + 1 guest, the guest owns a city and the host has a vehicle near it.
2. Raise an alarm for the guest's city on the host (`coop.civ alarm <city index>`): the host's vehicle is boosted (`boosts=1` in `coop.civ` status).
3. Within 20 s the host runs `coop.leave`, then loads a civ save again (the old vehicles are freed).
4. The host hosts again in Civilization (`coop.host`), and a guest joins.
5. On the first host tick, `boost_tick` writes into the old vehicle's memory. With a page heap (or an allocation of a different object at that address) this shows as an access violation or later corruption.

### Expected
The mod never touches a vehicle that is not in the game's own vehicle list. All raw-pointer maps of civ are cleared when the session ends or the world changes.

## Root cause
**What happens**

1. `h_OnAttacked` (host) puts every other player's vehicle within 600 m of an attacked player city into `g_boost` for 20 s, keyed by the raw `cVehicle*`.
2. Only `boost_tick` removes entries, and it runs only while `civ::active()` (civ mode and connected).
3. If the session ends or the world changes during those 20 s, the entries stay:
   - `do_leave` does not call `civ::on_session_end`.
   - `on_session_end` (disconnect path) does not clear `g_boost`.
   - civ has no `on_world_change`.
4. In the next civ session on that machine, the first `host_tick` calls `boost_tick` before `g_hv` is rebuilt from the game's vehicle list:
   ```cpp
   bool alive = g_hv.count(v) && game::object_alive(v);   // our map + a read of freed memory
   ...
   if (now > it->second.until) {
       F<float>(v, 0x218) = it->second.orig;               // write into the freed vehicle
   ```
   After `coop.leave`, `g_hv` is still the old map (`on_session_end` did not run), so `g_hv.count(v)` is true. `object_alive(v)` reads byte `+0x21` of freed memory, which says nothing about whether the block is still a vehicle.
5. The write hits whatever now lives at that address: heap corruption, and a crash far away later.

`g_siege`, `g_alarm_at`, `g_joint_told` and `g_city_gifted` also keep raw city pointers forever. They are only compared, never dereferenced, but a new city at a reused address inherits "gifted" or "already told" state.

### Confidence
CONFIRMED by reading the full path: map lifetime, call order and the missing clears. The write landing on reused memory is not observed at runtime. In normal play a dead vehicle is pruned from `g_hv` (via `kVDead`) before the game frees it, so the in-session window is small; the stale-across-session case is the real one.

## Fix
Fixed on fix/w1-tribe-civ (ba9dc46), as suggested, checked against today's code first: since
BUG-0033 `do_leave` already runs `civ::on_session_end`, but that cleared neither `g_boost` nor
`g_alarm_at` (confirmed in the game: after a `coop.leave` `boost=3 alarm_at=2`).

- `boost_tick` and `h_OnAttacked` write only to vehicles in the game's own vehicle list
  (`exists()`, cached per frame) instead of `g_hv.count(v) && object_alive(v)`.
- `on_session_end` clears `g_boost` (putting the speed back on vehicles still listed), `g_alarm_at`,
  `g_siege`, `g_joint_told`, `g_city_gifted`, `g_bfix_seen` and `g_ui_city`; `on_world_change`
  (BUG-0059) clears them without touching the freed vehicles.
- `coop.civ fix boost 0` restores the old check and the old session end (tests).

## Verification
Two-game test `tools/tests/coop_civ_reload.py` (suite `civ_reload`, civ_npc): an alarm boosts the
vehicles near the capital, the host reloads the planet while connected, alarm again, `coop.leave`;
`coop.civ tables` must have no boost or alarm entries after the load and after the leave.

- FAIL with `--nofix` (lane 5, by hand, out/lab/nofix-20261027/nofix_civ_reload.log): after the load
  `alarm_at=1`, after the leave `boost=3 alarm_at=2`.
- PASS with the fix: lab runs 20261027-184454 and 20261027-185915 (2x).

The write into a freed vehicle itself was not observed (no page heap on the lanes); the test checks
the condition that leads to it (a boost entry that outlives its world or session).

### Related
- civ has no `on_world_change` either (separate issue: stale `g_hv` / `g_host_cities` after a load).
- The same `object_alive` pattern is used in Tribe's `chiefs_tick` (separate issue).
- docs/re/civ.md, section "Co-op features (siege, roles, trade, alarm, superweapon)".

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #18.
- 2026-10-27: ticket opened from the issue.
- 2026-10-27: fixed on fix/w1-tribe-civ (ba9dc46), test civ_reload (FAIL with --nofix, PASS with the fix).
- 2026-10-28: released in 0.7.3.0.
