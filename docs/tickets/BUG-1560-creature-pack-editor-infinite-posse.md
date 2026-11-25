---
id: BUG-1560
title: Creature: guest pack recruit followed by Creature Editor leaks companion to host and breaks host pack size limit
status: fixed
kind: bug
stage: Creature
reported: 2026-11-25
source: Discord (#bug-reports, Smile / Szmajol)
role: host
versions: 0.8.0.1
fixed_in: 0.8.0.2
branch: w0802/creature-pack-cap
commits: d3601be9
tests: pack_editor_cap, pack_editor_cap_off, npc_tests
signature:
docs: docs/re/creature.md
related: BUG-1256
---

## Report
Discord (`Smile / Szmajol`): after the guest befriended/recruited a creature (from the host's species) and players entered the Creature Editor, the creature became the host's companion and the host could subsequently recruit an unlimited number of NPC creatures into their pack (exceeding the 3-member cap).

## Root cause
In `BUG-1256`, `npc::recruit_for_player()` added guest-recruited creatures to the host's `cPosseSimulator` with `cCreatureAnimal +0xE68 = 1` (`mbTempPosseMember`), which pushed the creature into `cGameNounManager::mPosseMembers` (`+0x5C`), set `c+0xB58 |= 0x100`, incremented `cPosseSimulator +0x1C` (`mNumTransientPosseMembers`), and skipped adding a portrait card to the host's `CreatureUI`. Four interacting defects caused companion leakage and pack-cap corruption across Creature Editor visits and saves:

1. **Premature pruning on editor exit (`host_pack_prune()`):** `host_pack_prune()` looked up each guest pack member via `host_npc(id)`, which requires `g_seen_now.count(c)` (updated on the exact current frame). On the first frame back from the Creature Editor (or any throttled frame), an alive pack member not yet in `creatures` returned `nullptr`, so `host_pack_prune()` deleted it from `g_pack_owners` without removing it from `cPosseSimulator` (leaving it as a host pack member with `mNumTransientPosseMembers` still incremented).
2. **In-place species evolution re-ID (`npc_object_reused`):** when the host evolved in the Creature Editor (`S_old -> S_new`), Spore updated creatures of the host's species in-place (same pointer and serial, new `species_instance`). `host_tick()` and `frozen_pooled_pass()` saw `npc_object_reused` and retired `old_id`, dropping `g_pack_owners` while the creature remained in `mPosseMembers`.
3. **Asymmetric save serialization (`cPosseSimulator` vs `cCreatureAnimal`):** disassembly of `SporeApp.exe` (`0x01365700..0x01365789`, `0x01583D30`) showed that `cPosseSimulator` serializes `+0x1C` (`mNumTransientPosseMembers`) and `cGameNounManager` serializes `mPosseMembers`, whereas `cCreatureAnimal +0xE68` (`mbTempPosseMember`) is never serialized (always reset to `0` by ctor `0x00C1FD81`). Any save (manual save, autosave, or migration world-share save) while a guest pack member was in `mPosseMembers` permanently leaked it into the host's pack with `+0xE68 == 0` while keeping `mNumTransientPosseMembers (+0x1C) >= 1`. Because `GetMemberCount` (`0x00D52DA0`) and `AddMember` (`0x00D5435C`) compute `mPosseMembers.size() - mNumTransientPosseMembers`, the host could recruit `3 + mNumTransientPosseMembers` permanent members (4, 5, ...), and removing a reloaded member (`+0xE68 == 0`) failed to decrement `+0x1C` (`RemoveMember` `0x00D534C8`), even leaving guests who loaded the migration save at `local_count = -1` (`list=0, temp=1`).
4. **`CreatureUI` rebuild (`0x00D2CBD6..0x00D2CC1A`):** when rebuilding the pack panel, Spore checks only `herd+0x192` (`mbSummonedHerd`), not `creature+0xE68`, leaking portrait cards for transient guest pack members into the host's UI.

## Fix
Implemented behind `COOP_FIX("npc.pack_editor_cap", "BUG-1560")` in `mods/coop/npc.cpp` and pure helpers in `mods/coop/social_logic.h`:
- **Live registry pruning (`host_pack_prune`):** check `g_hreg.find(id)` + `ref_alive(e->ref)` instead of single-frame `g_seen_now`, and cleanly call `host_pack_remove_creature(c)` if a dead member is still in `mPosseMembers`.
- **Species evolution ownership rebind (`g_pack_rebind`):** when `retire(old_id, c)` retires a guest-owned pack member whose creature object/serial is still alive, in `mPosseMembers`, and not pooled at the planet origin (note: `AddMember` detaches pack members from their herd via `0x00C6B5E0`, so `herd == nullptr` is normal), preserve ownership in `g_pack_rebind` (also honored by `ai_target`, `pack_count_of`, and `release_player_pack`) and rebind it to `new_id` on adoption in `host_tick()`.
- **Save-hook stripping (`savestrip::add_save_hooks`):** `pack_save_start()` temporarily removes guest-owned pack members from `mPosseMembers` via `cGameNounManager_RemovePosseMember` (`0x00B20330`), clears `+0xE68`, and decrements `mNumTransientPosseMembers (+0x1C)` before serialization; `pack_save_end()` restores them via `cGameNounManager_AddPosseMember` (`0x00B23430`) on the next frame with zero UI, audio, or AI disruption.
- **Host UI card guard (`h_UiAddPosse`):** hook `CreatureUI_AddPosseCard` (`0x00D2C2E0`) to refuse adding portrait cards on the host for guest-owned / pending-rebind / `mbTempPosseMember` creatures.
- **Defensive `cPosseSimulator` invariant reconciler (`posse_reconcile`):** on every Creature-stage tick (host and offline/solo, so already-corrupted 0.8.0.1 saves self-heal on load), remove orphaned guest temp members (`+0xE68 == 1`, `herd+0x192 == 0`, `+0xE6C == 0`, unowned) and clamp `*(int*)(posse + 0x1C)` to the true transient count in `mPosseMembers`.
- Added diagnostic counters to `coop.social pack` (`posse_diag: cap_fix list temp true_temp ui_blocked ui_leaked save_strips rebound orphans temp_fixed rebind_pending`).

## Verification
- Unit tests (`make unit-cpp unit-net unit-lint` under ASan+UBSan): `npc_tests` (`reconcile_temp_count`, `should_rebind_pack_member`, `is_orphan_guest_temp`, `should_block_host_posse_card`, `should_strip_pack_on_save`), `fixes_tests`, and `test_lint.py` all PASS.
- Two-game Wine lab ON/OFF twin on lane 3 (`out/lab/20261125-161729`):
  - `pack_editor_cap` (ON): **PASS** (151 s, 0 faults on both games across reload). Guest recruited host-species nest-mate `#2` (`065CBE70`); host evolved in Creature Editor (`065CBE70 -> 29312220`) and triggered migration world share: ownership rebound cleanly (`#2 -> #29`, `rebound=1`), `save_strips=3`, host stayed at `local_count=0 list=1 temp=1 true_temp=1 ui_leaked=0`, guest reloaded cleanly (`temp=0 local_count=1 my_pack=1 id=29`); after `session_restart(save=True)` host loaded clean (`list=0 temp=0 true_temp=0 local_count=0`); recruiting 4+ host NPCs capped strictly at 3 (`list=3 local_count=3`, 4th/5th refused).
  - `pack_editor_cap_off` (`SPORE_FIX_OFF=npc.pack_editor_cap`): **FAIL** (151 s) reproducing the exact bug: after Creature Editor, companion orphaned into host posse (`total=0 list=1 temp=1 local=0`), guest migration reload hit `temp=1 local_count=-1`, host save/reload persisted `list=1 temp=1 true_temp=0` (`+0xE68` lost while `+0x1C` serialized), and host recruited past the 3-member limit (`list=4 local_count=3`).
- Regression check on lane 3 (`out/lab/20261125-162251`): `guest_pack_recruit` (44 s) and `rivals_creature` (39 s) **PASS** (2/2, 0 faults).
