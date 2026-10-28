---
id: BUG-0063
title: flying to a partner who is in the galaxy view costs energy, can damage the ship and works while disabled
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#41
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-space
commits: 66d7a30, 4c74ae4
tests: space_map_input
signature:
docs: docs/re/space.md#space-w3-trips-clicks-defenders
related: #41
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/41 (full text, discussion).

When a partner flies in the galaxy view, its tag on the galaxy map and the "Fly there" button on the Space page send the player to the star next to the partner. That path calls the game's `SpaceTeleportTo` directly. It does not do the energy refund that the co-op jump does, so the trip spends the ship's energy, and with no energy left the game damages the ship. It also skips the "ship disabled" check. Severity: wrong behaviour (a free co-op trip that costs energy or HP, and an escape for a disabled ship).

### Where
- `go_to_partner`, the galaxy-view case: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_map.cpp#L795-L820
- Reached from the tag click and the Space page row: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_map.cpp#L822-L829 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_map.cpp#L907-L919
- What the co-op jump does around the same call: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L404-L417
- The disabled check that `travel` / `jump` have: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L297-L306

### How to reproduce
1. Host and guest in Space with the interstellar drive (`coop.space tool add interstellardrive`).
2. The guest zooms out to the galaxy view and flies somewhere (its place becomes 0).
3. The host zooms out to the galaxy view too and reads its energy (`ufo+0x7F4`).
4. The host clicks the guest's "(flying)" tag, or `coop.space map go <guest id>`.
5. The host's energy is lower afterwards. With energy drained to 0 first, the ship takes damage.
6. For the disabled case: make the host's ship disabled (`coop.space hit <dmg>` next to the partner), zoom out, then `coop.space map go <id>`: it travels.

### Expected
The trip to a partner is free, like the co-op jump (same energy and HP before and after), and refused while the ship is disabled.

## Root cause
**What happens**

1. `where_is(id, w)` gives `w.star == 0` when the partner's place is 0 (galaxy view). `go_to_partner` takes `nearest_star(w.pos)`.
2. If our own context is 2 (galaxy view), it calls:
   ```cpp
   cdecl_call<void>(a_Teleport, r);   // SpaceTeleportTo
   g_trips++;
   return "";
   ```
3. Per docs/re/space.md 7.3, `SpaceTeleportTo` refunds the money, then runs `cSimulatorPlayerUFO::TravelToStar`, which spends energy (`ChangeEnergy(-x)`) and, **with no energy left, damages the ship** through combatant vt+0x18.
4. `spacecoop::travel_to` handles exactly this: it raises `ufo+0x7F4` to the maximum (`+0x7F8`) before the call and puts the old value back after it ("the co-op jump is free"). `go_to_partner` does not.
5. `go_to_partner` also never looks at `spacecoop::disabled()`. While the ship is disabled (rescue, waiting for a partner) the player can still teleport away, which `jump` and `travel` refuse ("the ship is disabled"). In the other cases (`w.star != 0`, or not in the galaxy view) it goes through `jump` / `travel` and is fine.

### Confidence
CONFIRMED by reading the full path. The energy cost and the damage at 0 energy come from docs/re/space.md 7.3 (runtime-checked, and the reason `travel_to` has the refund). Not run at runtime for this path.

## Fix
Fixed on fix/w3-space (66d7a30): `go_to_partner` calls the new `spacecoop::teleport_in_galaxy(star)` in the galaxy view: refused while the ship is disabled or a cinematic plays, energy raised to the maximum around SpaceTeleportTo and put back, as `travel_to` does. `coop.space map teleportfix 0`: the old direct call (tests). Test commands: `coop.space energy [v]`, `coop.space disable 0|1`.

Suggested in the review:

Move the galaxy-view teleport into spacecoop, e.g. `spacecoop::teleport_in_galaxy(uint32_t star)`, which:
- checks `in_space()`, `g_down` and the cinematic guard,
- saves `ufo+0x7F4`, raises it to `+0x7F8`, calls `SpaceTeleportTo` and restores it, as `travel_to` does,
- returns "" or the reason.

`go_to_partner` then calls it. Keep the old direct call behind `coop.fix mapteleport off`.

## Verification
Done (2026-10-28, lane 6, GOG 2024): `coop_space_map_input.py`, both in the galaxy view, the guest's ship between stars. Disabled: `--without-fix` FAILS ("going", the host teleported next to the guest: distance 1.19 -> 0.04, /tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/waves/fix_w3-space/map_input_nofix.log); with the fix PASS ("the ship is disabled"). Energy: NOT reproduced: with energy 0 the old direct call cost neither energy nor HP (energy 0 -> 0, hp 1500 -> 1500 in the nofix run), so the energy and damage part of the report did not happen on this path (a short teleport to the star next to the partner); the refund is kept as a guard.

Planned in the review: Planned test (must FAIL without the fix and PASS with it):

Extend `tools/tests/coop_space_map.py` (or a new `coop_space_map_energy.py`):
1. Both in the galaxy view, the guest flying.
2. On the host, set the energy to a known low value and read it back from the game (`ufo+0x7F4`, a `coop.space hp` style read-out of energy).
3. `coop.space map go <guest id>`. Assert on the host that the energy and HP are unchanged and the active star is the one next to the guest.
4. Disable the host's ship and assert that `map go` is refused and the active star does not change.
5. Fails on main (energy drops; the disabled ship travels), passes with the fix.

### Related
- docs/re/space.md 7.3 (SpaceTeleportTo / TravelToStar rows, and the co-op jump recipe with the energy step).
- The same `travel_to_star` entry point for features (space_finale) also skips the disabled check; filed by the review of those features if relevant.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #41.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-space (66d7a30): disabled check (FAIL without / PASS with); the energy cost was not reproduced at runtime.
- 2026-10-28: fix/w3-space lab runs on lane 6: 20261028-010933 (14/14: the four new wave 3 Space tests and space_map, space_stale, space_fleet, space_puppet, space_rivalry, space_contracts, space_jump, space_guest_ui, space_allies, space_diplomacy), smoke 20261028-013951 (13/13; `ui` skipped: exclusive, needs a lane 1-5).
- 2026-10-28: released in 0.7.3.0.
