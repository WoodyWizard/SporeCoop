---
id: BUG-1553
title: Creature Rivals: GetRelationship enum inversion, War PvP and family combat, pack recruitment, arrows, transition and NPC Influence UI
status: fixed
kind: bug
stage: Creature
reported: 2026-11-25
source: v0.8.0 independent audit
role: both
versions: 0.8.0
fixed_in: 0.8.0.1
branch: w0801/creature
commits: 7e927911
tests: rivals_creature, rivals_creature_off, creature_rivals_tests, campaign_rivals
signature:
docs: docs/11-rivals.md
related: BUG-1547
---

## Report
Independent static audit of `BUG-1547` found critical gameplay blockers in Creature Rivals:
1. `creature_rivals_logic.h:29-35,272` defines `kRelHostile=1` (actually scared/flee!) and `kRelExtinct=5` (actually aggressive predator, causing normal predators to be marked permanently extinct).
2. War PvP and family combat are broken: `puppets.cpp:172,223-260` blocks hovering/colliding rival puppets; `settings_core.h:122-133` leaves co-op pack bonuses enabled; `npc.cpp:1953-1979` clears host pack/family targets against rival puppets; `npc.cpp:1321,2448-2450,3791` places host `NF_FAMILY` proxies into guest's swapped `avatar_herd()`.
3. Guest pack recruitment (`social.cpp:1042-1043,1073,1103`) reads host avatar's relations and shares `impressed`; guest attacks (`event==2`) are swallowed without lowering guest's wild relation (`creature_rivals.cpp:791-794`).
4. Partner edge arrows are off in Creature Rivals (`find.cpp:246-247`).
5. `trigger_native_tribe_transition()` is console-only (`creature_rivals.cpp:1475`), never invoked on race/draft end.
6. `host_assign_nests` scans all objects at 60 Hz (`creature_rivals.cpp:1205`); no NPC wild species list for F9 Influence UI.

## Root cause
See `docs/plan-0.8.0.1.md` WP-3.

## Fix
1. **GetRelationship enum & explicit extinction (`creature_rivals_logic.h`, `creature_rivals.cpp`)**:
   - Fixed constants to match Spore's verified `cSpeciesRelationshipManager::GetRelationship` values (`kRelScared = 1`, `kRelHostile = 2`, `kRelNeutral = 3`, `kRelFriendly = 4`, `kRelPredator = 5`, `kRelAllied = 6`) and added `is_hostile_rel()` treating both `2` and `5` as hostile.
   - Replaced `kRelExtinct = 5` inference with an explicit `SpeciesEntry::extinct` flag and `extinct_species` set populated only from `h_MakeExtinct`, `mark_extinct` / kill threshold, and `cHerd::mbExtinction (+0x190)` (only when all nest herds of a species have `+0x190 != 0`), with an optional trailing flag section on `kSkNpcRels`.
   - Updated `override_relationship`, `record_kill`, `record_attack`, and `step_toward_hostile` (`TurnAgainst`) so War/hostility writes `2` (aggressive/attack on sight) instead of `1` (scared/flee).
2. **War PvP & family combat (`puppets.cpp`, `settings_core.h`, `npc.cpp`, `creature_rivals.h`, `creature_rivals.cpp`)**:
   - Updated `puppets.cpp` (`partner_pass_tick`, `h_HoverFilter`, `h_CollEnabled`) to allow combat hover/targeting and collision against War rival puppets while keeping Neutral/Allied puppets blocked and social-stance hover skipped.
   - Set `off_in_rivals = true` for `kPackRescue`, `kRevive`, `kPackHeal`, and `kJointHunt` in `settings_core.h`.
   - Updated `npc.cpp` `pack_spares_partners` to stop clearing combat targets and stimuli against rival puppets when at War.
   - Updated `npc.cpp` (`send_spawn`, `MSG_NPC_SPAWN`, `proxy_herd`, `destroy_proxy`) and `creature_rivals.cpp` (`player_herd`, `note_proxy_family_owner`, `creature_family_owner`) so host and 3-4p rival family proxies carry their exact `family_owner` trailer and stay out of guest's swapped `avatar_herd()`.
3. **Guest pack recruitment, impressed isolation & wild attacks (`social.cpp`, `npc.cpp`, `creature_rivals.cpp`)**:
   - Updated `social.cpp` `is_recruit_candidate` in Creature Rivals to evaluate against the guest's own species, family ownership, and per-player wild relation table (`can_guest_recruit_in_rivals`) instead of host's avatar.
   - Added `PerPlayerImpressed` tracking (keyed by `(npc_id, object_serial)`) in `social.cpp`, gated guest `NF_IMPRESSED` mirror writes in `npc.cpp`, and restricted `K_RESULT` local impressed marking in Rivals to the acting player.
   - Wired guest `event == 2` wild attacks in `handle_host_rel_event` to `g_tables.record_attack(actor, key)` and pushed the updated table to that guest without polluting host's table.
4. **Partner edge arrows (`find.cpp`)**:
   - Enabled `arrows_on()` when `game::mode_id() == modes::kCreature && creature_rivals::active()`.
5. **Automatic Tribe transition (`creature_rivals.cpp`, `creature_rivals_logic.h`)**:
   - Wired `trigger_native_tribe_transition()` automatically on host once Creature Rivals race catch-up and draft finish (`should_auto_trigger_tribe_transition` with a 2.0 s settle window and `!game::cinematic_playing()` guard).
6. **Throttled `host_assign_nests` (`creature_rivals.cpp`)**:
   - Skipped full `game::for_each_object` planet scans once all connected players have assigned live nests, throttling periodic herd maintenance to >= 1.0 s while still reacting immediately to `BaseDistance` changes and unassigned players.
7. **F9 Influence UI wild species list (`creature_rivals.cpp`, `creature_rivals_logic.h`)**:
   - Implemented and registered `StageHooks::on_npc_ui_list` via `build_wild_npc_ui_list`, returning discovered non-extinct wild species (excluding player home species) with `Wild 0xXXXXXXXX` labels, `allied`, `hostile`, `can_turn = !extinct`, `can_protect = allied`, and `can_smooth = hostile`; verified `Protect` requires `kRelAllied`.
- `switch: rivals.creature` (`SPORE_FIX_OFF=rivals.creature`).

## Verification
- Native MinGW build (`make -j$(nproc)`): 0 warnings, 0 errors.
- Native Linux ASan+UBSan unit tests (`make unit-cpp`): all suites passed (`creature_rivals_tests`: 195 assertions covering enum values, predator `rel=5` non-extinction, War PvP/hover/settings, guest pack recruitment & impressed isolation, auto Tribe transition, NPC UI list generation, and wire codecs).
- Static lint (`python3 tools/tests/test_lint.py`): 1117 mod counter keys, 0 problems.
- Wine lab verification:
  - FAIL run: `out/lab/20261125-111531` (`rivals_creature_off` twin FAIL with `SPORE_FIX_OFF=rivals.creature`).
  - PASS runs: `out/lab/20261125-105211` (`rivals_creature` PASS 40 s), `out/lab/20261125-130606` (4-player `campaign_rivals` Creature -> Tribe natural transition).
