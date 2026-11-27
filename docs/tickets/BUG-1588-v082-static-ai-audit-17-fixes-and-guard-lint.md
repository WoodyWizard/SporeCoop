---
id: BUG-1588
title: Static disassembly audit of v0.8.2.2 hooks, vcalls, offsets, and guards (17 confirmed bugs fixed + static_guard_lint)
status: fixed
kind: bug
stage: All
reported: 2026-11-26
source: AI static audit swarm verified against GOG 2024 SporeApp.exe (dc04aee5) disassembly
role: both
versions: 0.8.0, 0.8.1, 0.8.2, 0.8.2.1, 0.8.2.2
fixed_in: 0.8.2.3
branch: main
commits: 6f5494d0
tests: static_guard_lint, rivals_tribe, rivals_tribe_wipe, rivals_civ, rivals_civ_spec_sw, rivals_space, rivals_creature, civ_planner, civ_build_refund
signature: 17 disassembly-verified bugs across v0.8.2.2 hooks/vcalls/offsets (cCivilization::~cCivilization double-free, cCity::ChangeOwner null civ, Cell::SpawnLoot res+0xD4 ptr level, FireSuperweapon void ABI, OOB +0x109C, meta+0x28 authorName)
switch: none (multi-stage static disassembly audit fixing memory safety, ABI, and guard bugs directly; regression-guarded by tools/static_guard_lint.py --self-test)
docs: docs/re/tribe.md, docs/re/civ.md, docs/re/space.md, docs/re/creature.md, docs/re/cell.md
related: BUG-1579, BUG-1580, BUG-1581, BUG-1582, BUG-1583, BUG-1586
---

## Report
After `BUG-1586` (wrong vtable slot `+0x0C` vs `+0xB8` on `cSpatialObject*`), a 5-agent static audit verified ~80 hooks, 90+ vcalls/thiscalls/struct offsets, and a repo-wide sweep of all `0x0C`/`0xB8` vcall and `game::cast` sites directly against GOG 2024 `SporeApp.exe` (`dc04aee5`) disassembly. All 17 findings (3 P0 crash, 7 P1 broken feature, 7 P2 logic/guard) were independently confirmed with zero false positives.

## Root causes & Fixes
### P0 Crashes
- **P0-1 (`mods/coop/civ_rivals.cpp`, `civ_rivals.h`)**: `ensure_guest_sw_tools` aliased `orig_player_civ()`'s `+0x470` and `+0x490` `eastl::vector` headers across frames, causing double-free/double-Release in `cCivilization::~cCivilization` (`0x00BF3830`) on teardown/exit. Fixed by hooking `~cCivilization` via live vtable slot 2 (`0x00BF4470`) to unconditionally un-alias before member dtors, and isolating per-civ `+0x490` `cGameTime` cooldown arrays (`IsolatedSwTimers`).
- **P0-2 (`mods/coop/space_rivals.cpp`)**: `host_cleanup_homeworld_civ_leftovers_task` called `cCity::ChangeOwner` (`0x00BE8A20`) with only `readable(c, 0x340)` without checking `oldCiv` (`[c+0x590]`) or `GetCivilizationByPoliticalID(mp)` (`newCiv`). Fixed by checking `a_CivByPol(nm, mp) != nullptr`, `readable(c, 0x594)`, and `F<void*>(c, 0x590) != nullptr`.
- **P0-3 (`mods/coop/cell_rivals.cpp`)**: `drop_rival_meat` fallback passed interior pointer `res + 0xD4` instead of dereferenced `*(void**)(res + 0xD4)` (`cCellResourceRef*`) as arg 7 of `Cell::SpawnLoot` (`0x00E771D0`), causing an immediate AV on early PvP kills. Fixed dereference + readability guard and widened `buf[64]` to `buf[256]`.

### P1 Broken Features
- **P1-1 (`mods/coop/tribe_rivals.cpp`)**: `guest_has_selected` read `+0x109C` (100 bytes past `sizeof(cCreatureCitizen) == 0x1038`) instead of `0x12C` (`tribe::kSelectedOff`). Fixed to `0x12C`.
- **P1-2 (`mods/coop/tribe_rivals.cpp`)**: wild animal (`0x18EB45Eu`) returned `0x0A` (Attack citizen) instead of `0x0B` (Hunt, attack mode) / `0x29` (Tame, social mode). Fixed.
- **P1-3 (`mods/coop/civ.cpp`)**: `cCivilization::FireSuperweapon` (`0x00BF8BB0`) returns `void`, not `bool`; reading garbage `eax` caused false charges on failure and 1/16 free fires on success. Fixed detour signature to `void` and detected actual firing deterministically via wealth/timer/global-flag state delta.
- **P1-4 (`mods/coop/civ.cpp`)**: Together-mode Civ planner building/turret sales refunded `$0` and bypassed `g_held_builds`/`refund_build`. Allowed `a_PlannerMoneyAddRet` when `planner_city() != nullptr` and added `g_held_planner_syncs` + `together_refund_build()`.
- **P1-5 (`mods/coop/space_rivals.cpp`)**: `ChangeOwner` skipped `City_SetPartsOwner` when `[city+0x2E2] == 0` (leaving rival turrets hostile), and overwriting foreign `cCivData+0x14 = mp` created duplicate `pol == mp` records. Set `city+0x2E2 = 1` before `ChangeOwner` and prevented duplicate `cCivData` records.
- **P1-6 (`mods/coop/creature_rivals.cpp`)**: `override_relationship` substituted `game::avatar()` on `(nullptr, nullptr)` / `(avatar, nullptr)`, forcing `kRelAllied (6)` on species-only queries (breaking egg steal & nest checks). Restricted substitution to non-avatar single-null queries with `pa != pb`.
- **P1-7 (`mods/coop/creations_library.cpp`)**: `decode_png_info` read `meta+0x28` (`authorName`) instead of `meta+0x40` (`creationName`). Fixed offset to `0x40` and raised UTF-16 cap from 128 to 256.

### P2 Logic, Guards & Static Linter
- **P2-1 (`mods/coop/tribe_rivals.cpp`, `tribe_host.cpp`)**: closed food-bundle (`0x018C431C`) steal bypass on Neutral/Allied/shielded rival tribes.
- **P2-2 (`mods/coop/tribe_rivals.cpp`, `tribe.cpp`)**: fixed off-by-4 `readable` guards (`+0xB50 -> +0xB4C`, `+0xE80 -> +0xE7C`), added `comb` vtable check, and added `LIFECYCLE_STATE` reset for `g_cons_active[8]`.
- **P2-3 (`mods/coop/civ.cpp`)**: checked Rivals affordability before spawning structures in `kCmdPlannerSync` with rollback on charge failure.
- **P2-4 (`mods/coop/civ.cpp`, `civ_rivals.cpp`, `civ_rivals.h`)**: recorded `g_last_local_spend` only for `civ_rivals::my_civ()` and required `polB == me && polA != me` in `on_rel_apply`.
- **P2-5 (`mods/coop/space_rivals.cpp`)**: cleared any non-zero `F<int>(bld, 0x154)` in `restore_protected_city_hp`.
- **P2-6 (`mods/coop/space_contracts.cpp`)**: published `S_NONE` on host `kWorldChange` when connected and rejected manual `offer()` in Rivals.
- **P2-7 (`mods/coop/creature_rivals.cpp`)**: fixed off-by-4 `readable` guards (`sp + 0x56C`, `tun + 0x408`) and guarded all 4 `herd+0x40` `mb[i]` loops.
- **Static Linter (`tools/static_guard_lint.py`, `Makefile`)**: added build-time linter catching undersized/off-by-N `readable` guards and wrong `vcall` byte-offset / `cSpatialObject` `+0x0C` casts, wired into `make unit-lint`.

## Verification
- PASS runs: `out/lab/20261126-212912` (`rivals_tribe`, `rivals_tribe_wipe`, `rivals_civ`, `rivals_civ_spec_sw`, `rivals_space`, `rivals_creature`: 6/6 PASS, 0 faults; `rivals_cell` meat drop 3/3 chunks with 0 faults), `out/lab/20261126-213147` (`civ_planner`, `civ_build_refund`: 2/2 PASS, 0 faults), plus `make test` (all 10 suites including `python3 tools/static_guard_lint.py --self-test`: 6 bad + 6 good cases passed).
- no-fail-run: umbrella ticket for 17 static disassembly-verified crash/ABI/guard fixes (including teardown double-free, early-kill null-cache AV, and undersized `skg::readable` guards) verified directly against GOG 2024 `SporeApp.exe` (`dc04aee5`) and guarded by `tools/static_guard_lint.py --self-test`.
