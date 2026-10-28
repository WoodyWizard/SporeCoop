---
id: BUG-0134
title: new_stage_game.py --npc-cities uses GOG 2016-only symbols, and 0xAE3A80 has two contradicting signatures
status: fixed
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#113
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits: 880c7df (part 2); afdcc31 on main (part 1, fix/w2-civ)
tests: new_stage_game.py civ --npc-cities 3 (manual, lane 3); coop_civ_city_join / coop_civ_ai_spawn
signature:
docs:
related: #113
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/113 (full text, discussion).

This issue covers two related problems.

- `tools/tests/new_stage_game.py civ --npc-cities N` calls `Simulator::cCitySimulator::Get` and `Simulator::cCitySimulator::AppearNextCity`. These names exist only in the frozen GOG 2016 overrides. On GOG 2024, the only supported build, the same functions are named `Simulator::cCivExpansion::Get` / `SpawnNextCity`. The script stops with "FAILED: no city simulator", so the civ fixture with NPC cities cannot be rebuilt.
- The same function 0xAE3A80 has two `.sym` entries with opposite argument orders. The mod's detour names its arguments in the reverse of the order that was checked at runtime.

### Where
- Script: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/new_stage_game.py#L98-L105
- Only in GOG 2016:
  - `overrides_922af7c7.sym` L281 `Simulator::cCitySimulator::Get=0x00B268A0`
  - L282 `Simulator::cCitySimulator::AppearNextCity=0x00AE3A80 ; verified ... thiscall(cs, bool instant, void* who) ret 8 ... Argument order checked: who != 0 is dereferenced (who=1 crashed)`

  https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/data/symbols/overrides_922af7c7.sym#L281-L282
- The same address in GOG 2016, under the other name: L85-L86 `Simulator::cCivExpansion::Get=0x00B268A0` and `Simulator::cCivExpansion::SpawnNextCity=0x00AE3A80 ; likely thiscall(obj, bool) ret 8` https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/data/symbols/overrides_922af7c7.sym#L85-L86
- GOG 2024 has only the cCivExpansion names: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/data/symbols/overrides_dc04aee5.sym#L80-L81
- docs/re/civ.md L542: "AppearNextCity 0xAE3A80 thiscall(cs, bool instant, void* who) ret 8 -> bool ✔ runtime" https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/civ.md#L542
- The detour names the arguments `(void* who, int flag)`, i.e. reversed: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L436-L452
- Fixture note that depends on the script: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/fixtures.py#L22

### How to reproduce
**How to reproduce / check**

`tools/tests/new_stage_game.py civ --npc-cities 3` on a GOG 2024 lane prints `FAILED: no city simulator`. The contradiction can be checked by reading the two `.sym` lines and civ.md L542.

### Expected
The script works on GOG 2024, and there is one name and one documented argument order for 0xAE3A80 that the detour follows.

## Root cause
**What happens**

1. On a GOG 2024 lane, `call Simulator::cCitySimulator::Get cdecl` finds no symbol, the script prints "FAILED: no city simulator" and exits 1. The `--npc-cities` path of `coop_join_new_game.py` and the civ fixture recipe in fixtures.py cannot be redone.
2. The argument order: `h_SpawnNextCity(ex, who, flag)` uses `!who && !flag` to decide that the host founded the next site. The real order is `(instant, who)`. Both game callers and `civ.cpp`'s own replay pass (0, 0), so the behaviour is right today. The only non-zero caller is `new_stage_game.py:105` (`1 0`, instant). Any future code that reads `who` from the detour gets the `instant` flag, and the other way round. A real `who` (a pointer) passed as "flag" reads as non-zero and skips the broadcast.

### Confidence
CONFIRMED by reading both sides, and a second reviewer verified it. Low severity: tooling, plus a naming contradiction that does no harm today.

## Fix
1. The script: fixed in wave 2 (afdcc31, BUG-0120's branch): it calls `Simulator::cCivExpansion::Get`
   / `SpawnNextCity`, the GOG 2024 names.
2. The signature: the runtime-checked order `(ex, bool instant, void* who)` holds. The detour is
   `h_SpawnNextCity(void* ex, int instant, void* who)` now (and the replays call `(ex, 0, nullptr)`);
   no behaviour change (all callers pass zeros, the broadcast condition is symmetric). The GOG 2016
   "likely (obj, bool)" entry got a CORRECTION note pointing at the verified one, the GOG 2024 entry
   names the arguments. docs/re/civ.md#civ-spawn-next-city-args.

## Verification
- 2026-10-28, lane 3, this branch: a fresh profile, `new_stage_game.py civ --npc-cities 3`:
  `cities: 1 -> 4`, rc 0 (main 563f72d printed "FAILED: no city simulator", per the review).
- The rename is binary-identical in behaviour (two 4-byte arguments, symmetric condition): a
  written argument, plus the civ city tests run on lane 3 (see the branch report).

### Related
#64 (re_lint gaps).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #113.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-tools (wave 4, tests and tooling).
- 2026-10-28: released in 0.7.3.0.
