---
id: BUG-1576
title: Civ/Creature: crash at 0x00C11CEF in cCreatureBase::GetBoneTransform when mpAnimatedCreature (+0xB54) is null
status: fixed
kind: bug
stage: Civ
reported: 2026-11-26
source: Discord (Vlaid18)
role: host
versions: 0.8.1.1
fixed_in: 0.8.1.3
branch: main
commits: 8021d561
tests: creature_bone_null, creature_bone_null_off
signature: 0xC0000005 at 0x00C11CEF (Steam 0x00B81CEF) read 0x0000017C
docs: docs/re/creature.md
related: BUG-0285, BUG-0310
---

## Report
Discord (`Vlaid18`): three reproducible Civilization stage crashes (Civ Together and Civ Rivals on save `Дедство.spo`, crashing 14 ms after unpausing on the first simulation frame) at Steam `0x00B81CEF` (GOG `0x00C11CEF`) reading `0x0000017C` with `eax=0`.

## Root cause
Disassembly of `SporebinEP1/SporeApp.exe` (GOG 2024 `dc04aee5`):
- Caller `0x00C199D0` (`cCreatureBase::cCombatant::GetHitPosition(out_vec, int bone_idx)`) only checks `[creature+0xB54] != 0` when `bone_idx == -1`. When called with a concrete bone index (e.g. `bone_idx = 25`, `ebx = 25 * 0x2BC = 0x445C`), it skips the null check and calls `0x00C11CB0(out_pos, local_pos, bone_idx, true)`.
- `0x00C11CB0` (`bool __thiscall cCreatureBase::GetBoneTransform(void* creature, float* out_pos, float* out_quat, int bone_idx, uint8_t world_space)`, `ret 0x10`) only checks `bone_idx == -1` (returning `false` at `0x00C1209C`). Both `world_space == true` (`0x00C11CE3`) and `world_space == false` (`0x00C11EB0`) blindly dereference `[creature+0xB54]` (`mpAnimatedCreature`), `[ac+0x17C]` (`mpModel`), and `[mdl+0x2E4] + bone_idx*0x2BC`.
- Neighbor `0x00C120B0` (`bool __thiscall cCreatureBase::AttachToBone(void* creature, int bone_idx, void* obj, void* a3, void* a4)`, `ret 0x10`) has the exact same missing `+0xB54` / `+0x17C` / bone-slot validation.

## Fix
- Added verified symbols `SporeCoop::Creature_GetBoneTransform=0x00C11CB0` and `SporeCoop::Creature_AttachToBone=0x00C120B0` in `data/symbols/overrides_dc04aee5.sym`.
- Added `COOP_FIX_UNSAFE("game.creature_bone_null", "BUG-1576")` (default ON) in `mods/coop/gamefixes.cpp` hooking both functions via `bone_xf_guard` / `bone_attach_guard`: validates `bone_idx` bounds, `mpAnimatedCreature (+0xB54)`, `mpModel (+0x17C)`, and the `0x2BC`-stride bone slot readability, returning `false` cleanly instead of dereferencing null.
- Added `coop.boneguard [probe]` command for status inspection and deterministic Wine lab verification.

## Verification
- Two-game Wine lab ON/OFF twin (`out/lab/20261126-142431`, `coop_creature_bone_null.py`):
  - ON (`creature_bone_null`): **PASS** (23 s, `hooked=1 fix=1`, `probe: ret=0 xf_guarded=1`, 0 faults).
  - OFF (`creature_bone_null_off`, `SPORE_FIX_OFF=game.creature_bone_null`): **FAIL** (23 s) with exact `0xC0000005 at 0x00C11CEF SporeCoop::Creature_GetBoneTransform+0x3F data=0x0000017C`.
