---
id: BUG-1540
title: Creature: guest wiping a hostile nest undiscovered by the host misses the extinction goal DNA
status: open
kind: bug
stage: Creature
reported: 2026-11-24
source: Discord bug report 2026-11-24
role: guest
versions: 0.7.16, 0.7.16.1, 0.7.17.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md
related: BUG-0205, BUG-0699
---

## Report
In Creature stage, a guest hunting and wiping a hostile species' nest far from the host (~550 m away) receives only the per-kill hunt DNA (+5 per creature) and misses the species extinction goal card reward (~50-70 DNA).

In the report log (`2026-11-24`), killing 7 members of species `067C6748` incremented `killed` from `0` to `7` while the host's species relationship table kept `relation = 0` throughout (`relation 0 -> 0, killed 0 -> 1 .. 6 -> 7`), and no extinction goal reward was paid. Earlier in the same session, species `067C65C5` likewise stayed at `relation = 0` for kills `0..4` until the host approached it (`relation 0 -> 5`), whereas nests whose species already had `relation = 5` in the host's table paid their goal DNA normally.

## Root cause
Verified from EP1 disassembly (`SporeApp.exe`, GOG 2024 / Steam):

1. When a player approaches a species in single-player, the species encounter/discovery function (`0x00BA5130`) queries `GetRelationship(mgr, key, creature, nullptr)` (`0x00BA40E0`) and writes the resolved default relation (e.g. `5` for a naturally hostile predator species, `[tuning+0x390]`) into `rec[0]` of the species relationship table (`mgr+0x20`).
2. When a guest fights a naturally hostile species far from the host, the host never runs `0x00BA5130` for that species, so `rec[0]` remains `0` ("use archetype default") on the host (and `enforce_table()` in `mods/coop/social.cpp` overwrites the guest's `rec[0]` back to `0` from `MSG_SPECIES`).
3. On each lethal hit credited on the host, `cSpeciesRelationshipManager::ApplyEvent` (`0x00BA4A00`, events `0`/`5`) computes `old_rel = GetRelationship(mgr, key, creature, nullptr)` -> `5` (via `creature->tuning+0x390`). When `killed` reaches the extinction threshold `[tuning+0x400]`, it sets `new_rel = 5`, calls `SpeciesExtinct` (`0x00BA4750`), and then at `0x00BA4CDB` compares `new_rel == old_rel` (`5 == 5`), jumping straight past `mov [rec+0], esi` (`0x00BA4D38`). Thus `rec[0]` remains `0` on the host even after extinction.
4. `SpeciesGoal_Update` (`0x00C2EE80`) calls `GetRelationship(mgr, &goal->key, nullptr, nullptr)` (`0x00C2EED7`) with a null creature pointer, which returns `0` when `rec[0] == 0`. At `0x00C2F028` it checks `relation == 5` before paying the extinction goal DNA (`[goal+0x98]`), so `0 != 5` skips the payout on the guest. Even if `K_GOAL` were sent, `goal_outcome()` on the host (`mods/coop/social.cpp`) also passes null creature pointers to `GetRelationship` and would reject `0 != 5`.

## Fix
(planned in 0.7.99 WP-F1) In `h_RelEvent` (`mods/coop/social.cpp`, gated by a new `coop.fix` switch), on the host when `rec[0] == 0` and `creature != nullptr`, resolve the effective relation via `GetRelationship(mgr, key, creature, nullptr)` and materialize `rec[0]` before/around `o_RelEvent` so `rec[0]` matches what discovery (`0x00BA5130`) would have stored.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-24: opened from Discord bug report; root cause confirmed in EP1 disassembly (`0x00BA4A00`, `0x00BA5130`, `0x00C2EE80`).
