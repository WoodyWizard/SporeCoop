---
id: BUG-0031
title: vehicles of a player who left stay theirs, nobody can order them, and captured cities can go to them
status: fixed
kind: bug
stage: Civ
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#9
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-civ
commits: 7f066e4
tests: civ_leave
signature:
docs: docs/re/civ.md#civ-wave3
related: #9
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/9 (full text, discussion).

In the Civilization stage, a guest who leaves the session keeps the vehicles they owned. Nobody else (not even the host) can select or order those vehicles for the rest of the session. A city captured next to them can also be given to the departed player, and then nobody can buy or build in it. Because session ids are reused from the lowest free one, the next player who joins silently inherits those vehicles (and cities). Severity: wrong behaviour.

### Where
- Vehicle owners are only set when a vehicle is first seen, bought or given, and are never re-split: [civ.cpp#L971-L988](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L971-L988), [civ.cpp#L1180-L1184](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1180-L1184), [civ.cpp#L1230-L1237](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1230-L1237)
- Cities are re-split when the player set changes, vehicles are not: [civ.cpp#L822-L856](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L822-L856)
- Selection filter: [civ.cpp#L211-L219](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L211-L219), and `drop_foreign_selection`: [civ.cpp#L914-L925](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L914-L925)
- Capture gives the city to the owner of the nearest vehicle, absent or not: [civ.cpp#L480-L499](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L480-L499)
- Session ids are reused (lowest free id): [net/session.cpp#L118-L125](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L118-L125)
- For comparison, Tribe re-splits all members when the player set changes: [tribe.cpp#L396-L405](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L396-L405)

### How to reproduce
1. Civilization stage, host + 1 guest (or host + 2 guests to see it with a remaining guest).
2. The guest buys 2 vehicles in their own city (`coop.civ buy <city> 0`).
3. The guest leaves (`coop.leave` or closes the game).
4. On the host, try to select those vehicles: the selection is dropped at once. `coop.civ` lists them with `owner=1`.
5. Optional: drive host vehicles next to the departed guest's vehicles, capture an NPC city with the departed player's vehicle nearest to it, and open the city's menu: "this city belongs to ...".

### Expected
When a player leaves, their vehicles go to the remaining players (for example, the owner of the nearest player city, or the host), like Tribe members and Civ cities. A capture never names an absent player.

## Root cause
**What happens**

1. The host's `g_hv` maps each vehicle to `{id, owner}`. The owner is set once: by the nearest player city when the vehicle first appears, by `CMD_BUY` for the buyer, or by `CMD_GIVE`.
2. When a guest leaves, `host_tick` sees `players != g_split_for` and `host_assign_cities` re-splits the cities. Nothing touches `g_hv`, so the vehicles still have the departed id as their owner.
3. On the host, `h_SetSelected` turns a selection into `sel = 0` when `owner != 0xFF && owner != my_id`. `drop_foreign_selection` also deselects such vehicles every 0.1 s. So the host cannot use them. Remaining guests are refused the same way (`own_vehicle` in `host_command` needs `owner == from`).
4. They still count against the civilization's 36-vehicle cap (`VehicleSlotsLeft`), so fewer vehicles can be bought.
5. `h_Capture` picks the owner of the vehicle closest to the captured city among all `g_hv` entries with `owner != 0xFE`. That includes the departed id:
   ```cpp
   for (auto& [v, hv] : g_hv) {
       if (hv.owner == 0xFE) continue;
       ... owner = hv.owner;
   }
   g_city_owner[city] = owner;
   ```
   `host_assign_cities` only re-splits when the player set changes, so that city stays owned by an absent player. `h_UIMessage` then answers "this city belongs to ..." to everyone.
6. Ids are reused, so the next joiner gets the old id and with it the vehicles (and such cities) of a different person.

### Confidence
CONFIRMED by reading the full path: owner writes, selection filter, command check and id reuse. Not run on the lab.

## Fix
Fixed on fix/w3-civ (7f066e4): when the player set changes, the host gives every vehicle whose
owner is not a present player (and not 0xFE) to the owner of the nearest player city after the new
city split (the shared capital or none: the host). `h_Capture` skips vehicles of absent owners.
`coop.civ fix orphans 0`: the old behaviour (tests).

## Verification
`coop_civ_leave.py` (suite `civ_leave`, 2 instances): the guest buys a vehicle and leaves; on the
host the vehicle's owner, the game's selected flag 1.5 s after `coop.civ select` and a move order.
FAIL with `--nofix` (owner 1, selection dropped), PASS with the fix (owner 0, stays selected, drives
39.7 m), lane 7. The capture part is covered by the same owner check (no vehicle with an absent
owner is left for `h_Capture` to pick).

### Related
- Same pattern for gifts: the gift target is not validated (separate issue).
- docs/re/civ.md, section "Co-op features (siege, roles, trade, alarm, superweapon)".

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #9.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-civ (7f066e4), test civ_leave (FAIL with --nofix, PASS with the fix).
- 2026-10-28: released in 0.7.3.0.
