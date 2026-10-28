---
id: BUG-0046
title: armada contract starts a second raid when the host comes back, and the old raid decides the wave
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#24
role: host
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-space
commits: 9f9fd90, 4c74ae4
tests: space_armada_return
signature:
docs: docs/re/space.md#space-w3-trips-clicks-defenders
related: #24
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/24 (full text, discussion).

In an armada co-op contract the mod does not remember which game raid belongs to the current wave. If the host leaves the colony during a wave and comes back, a second raid starts on the same planet while the first one still runs. The end of either raid then decides the wave, and the ships of both raids get HP x2. Severity: wrong behaviour. A contract can fail ("the pirates won a wave") or be won while ships are still fighting, and a wave can be twice as big as intended.

### Where
- wave reset when the host leaves the planet: [space_contracts.cpp#L776-L785](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_contracts.cpp#L776-L785)
- a new raid when the crew is back: [#L847-L864](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_contracts.cpp#L847-L864), `start_raid`: [#L759-L767](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_contracts.cpp#L759-L767)
- ship match by mission id and planet only: [#L745-L753](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_contracts.cpp#L745-L753), HP doubling: [#L789-L800](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_contracts.cpp#L789-L800)
- wave result from the raid listener: [#L970-L974](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_contracts.cpp#L970-L974), wave end: [#L828-L844](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_contracts.cpp#L828-L844)
- the listener gets only (place, state): [spacecoop.cpp#L517-L534](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L517-L534)

### How to reproduce
Two players (host + 1 guest), allies or rivals mode, Space, the host has a colony:
1. `coop.space contracts offer armada here` on the host's colony. Both accept and both are on the planet; wave 1 starts (`coop.space allies raids` on the host lists one raid on the planet).
2. Before the ships are destroyed, the host leaves to the system view for a few seconds and lands again (the guest stays or comes back too).
3. After 3 to 5 s, `coop.space allies raids` on the host lists two active raids on that planet, and `coop.fleet list` shows twice the ships.
4. Let the first raid end off-screen, or end it with the game's own state change: the contract result follows that raid, not the wave being fought.

### Expected
One raid per wave. When the host comes back, the wave continues with the raid that is still running (or a new one only after the old one has ended). The wave result comes only from that raid, and each ship's HP is raised once.

## Root cause
**What happens**

1. With the crew together on the host's colony, `armada_tick` calls `start_raid`, which is the game's `raid -start` routine. It creates a cMission 0xA4B0A2DF on the colony and sets `g_wave_on`. The mission pointer is not kept.
2. The host flies from the planet to the system view. `place != g_wave_place`, so the wave is reset (`g_wave_on = false`, `g_seen.clear()`, `g_next_wave = t + 5`). The code comment says the game keeps running that raid off-screen, and the mission stays active.
3. The host returns. `ready` is true again 3 s later and `start_raid` creates a second raid on the same planet. Nothing checks for a raid that is still active there, unlike `space_allies.cpp`, which looks at the mission list.
4. `armada_ship(u)` accepts any UFO whose owner mission has id 0xA4B0A2DF and targets that planet, so the ships of both raids count as this wave. Each one is "new" to the cleared `g_seen` and gets `kMaxHp *= 2`.
5. The raid listener stores the first end state (5 or 6) of any raid on that planet into `g_wave_result`. When the old raid ends while the new wave runs:
   - state 6 (the colony plundered off-screen) calls `finish(false, R_PLUNDERED)`;
   - state 5 ends the wave as won, and moves to the next wave or `finish(true)` while ships are still alive.

### Confidence
CONFIRMED by reading the full path: no mission identity is kept; the listener and `armada_ship` filter only by place and mission id; a new raid starts after a return. PLAUSIBLE: that the game really keeps the first raid active off-screen and lets a second raid start on the same planet. The code comment at L778-L779 says it does, but this was not run.

## Fix
Fixed on fix/w3-space (9f9fd90): `start_raid` notes the raid missions on the colony before and after the game's raid starter and keeps the new one as the wave's mission. `armada_ship` and the raid listener (which now gets the mission: `spacecoop::set_raid_listener(fn(mission, place, state))`) take only that mission. When the crew is back on the colony and that mission is still in the game's mission list in state 2 or 3, the wave resumes it instead of starting a raid. HP is raised once per ship of the wave's mission (a set kept over the return). The mission list comes from space_allies (`spaceext::game_missions`). `coop.space contracts wavefix 0`: the old way; `coop.space contracts raids` lists the raids on the colony (tests); `coop.space takeoff` (tests) leaves the planet.

Suggested in the review:

- After `start_raid`, find the new cMission in the mission list (as `space_allies.cpp` `active_raids` does) and keep its pointer for the wave. Filter the listener and `armada_ship` by that mission (`ufo +0x800 == wave mission`), and check the pointer is still in the list before using it.
- On a return, if the wave's mission is still active (state 2 or 3), resume the wave on it instead of calling `start_raid`.
- Keep the HP multiplier per ship once per contract (a set kept across the reset), not per `g_seen`.
- Put the old behaviour behind `coop.fix armada_wave_mission off`.

## Verification
Done (2026-10-28, lane 6, GOG 2024): `coop_space_armada_return.py` (suite `space_armada_return`): armada accepted, both land, wave 1 comes, the host takes off to the system view for 6 s and lands again. `--without-fix` FAILS: two active raids on the colony after the return, 30 ships taken for the wave (/tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/waves/fix_w3-space/armada_nofix2.log); with the fix PASS: one raid, `resumed=1` (/tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/waves/fix_w3-space/armada_pass2.log). The game does keep the first raid active (state 3) while nobody is on the planet (confirms the PLAUSIBLE part of the root cause). Runtime finding: during the wave the mouse wheel did not take the host off the planet (the test uses `coop.space takeoff`). Not run: the old raid ending with 6 while a second runs (no longer possible with the fix: there is no second raid).

Planned in the review: Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_space_contracts.py`, new step "host leaves mid-wave":
1. Two lanes. Start an armada contract and wave 1 as the existing test does.
2. The host leaves the planet for 5 s and lands again.
3. Assert on the host's game, through the mission list: exactly one active raid mission on the colony. Assert through the UFO list on both games: each raid ship's max HP is exactly 2x its type's base.
4. Then end the old raid with state 6 (`coop.space allies raids` gives the pointer; set its state with the game's SetState). The contract must stay active.

This must FAIL on main and with `coop.fix armada_wave_mission off`, and PASS with the fix. No crash report on either game.

### Related
- docs/re/space.md 6 (raids), 10.2 (the armada: the game's own raids).
- docs/known-issues.md "Space contracts: what is not done".
- The same "raid identified by its planet only" pattern in `spacecoop.cpp` `h_SetState` (raid alerts and the defended bonus).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #24.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-space (9f9fd90): the wave keeps and resumes its own raid; space_armada_return FAIL without / PASS with.
- 2026-10-28: fix/w3-space lab runs on lane 6: 20261028-010933 (14/14: the four new wave 3 Space tests and space_map, space_stale, space_fleet, space_puppet, space_rivalry, space_contracts, space_jump, space_guest_ui, space_allies, space_diplomacy), smoke 20261028-013951 (13/13; `ui` skipped: exclusive, needs a lane 1-5).
- 2026-10-28: released in 0.7.3.0.
