---
id: BUG-1562
title: Rivals: isolate designs.cpp outfit/design sync and block Space Jump to non-allied rivals
status: fixed
kind: bug
stage: All
reported: 2026-11-25
source: 0.8.1 Rivals isolation audit
role: both
versions: 0.8.0.2
fixed_in: 0.8.1
branch: w081/rivals-designs-jump
commits: 6cf6dbc5
tests: rivals_designs_jump, rivals_designs_jump_off
signature:
docs: docs/11-rivals.md
related: BUG-0072, BUG-1156, BUG-1548, BUG-1550
---

## Report
1. `mods/coop/designs.cpp` has zero Rivals awareness: in Tribe Rivals, editing the host's outfit in Tribal Outfitter runs `apply_outfit()` onto `player_tribe()` on the guest, which after perspective swap is the guest's own rival tribe (overwriting the rival's clothing). Similarly, host Civ/Space designs should not overwrite a rival's designs in Rivals mode.
2. `mods/coop/spacecoop.cpp` (`pick_jump_target` / `can_jump` / `jump`) does not check `spaceext::rivals()` or player relations: in Space Rivals, a player can click Jump To and teleport directly into a Neutral or War rival's star system, bypassing drive range and exploration.

## Root cause
- `designs.cpp` never checked `tribe_rivals::active()` / `civ_rivals::active()` / `spacerivals::active()` / `spaceext::rivals()` before applying host outfit or civilization design updates onto a guest, or before refusing/skipping native reload for guest editors.
- `spacecoop.cpp` (`pick_jump_target`, `can_jump`, `jump`, and `squad_rows`) treated every connected partner as an eligible jump target regardless of Rivals diplomacy state.

## Fix
- Added `COOP_FIX("rivals.designs_isolate", "BUG-1562")` in `mods/coop/designs.cpp`:
  - Skips `outfit_sync` / `apply_outfit` (and host `host_species_out`) when `tribe_rivals::active()`, and skips `civ_sync` when `civ_rivals::active()` or Space Rivals is active, incrementing diagnostic counter `rivals_skipped` exposed in `coop.designs`.
  - Allows guest Tribal Outfitter (`h_EditorReturn`) to reload the guest's own perspective-swapped `player_tribe()` natively in Tribe Rivals, and allows guest editors in `refuse_own_editor` during active Rivals stages.
- Added `COOP_FIX("rivals.space_jump_ally", "BUG-1562")` in `mods/coop/spacecoop.cpp` and extended `spacelogic::JumpPartner` / `spacelogic::pick_jump_target` in `mods/coop/space_logic.h`:
  - When Space Rivals is active (`spaceext::rivals() || spacerivals::active()`), only partners with `Relation::Ally` are eligible jump targets.
  - `can_jump(to)` and `jump(0xFF)` skip Neutral/War rivals; explicit `jump(to)` or `jump(0xFF)` with only non-allied rivals returns `"cannot jump to a rival (allies only in Rivals mode)"` without calling `travel_to()`, and increments `jump_rivals_blocked` in `coop.space` status.
  - `squad_rows` hides the `"Jump there"` button for non-allied rivals in Space Rivals.

## Verification
- Native unit tests (`make unit-cpp unit-net unit-lint`, `tests/space_tests.cpp::test_jump_rivals`) and `python3 tools/tests/test_lint.py`: PASS.
- Wine lab ON/OFF twin run on lane 2 (`out/lab/20261125-173848/summary.md`):
  - `rivals_designs_jump` (ON): PASS (60 s) - Space Rivals Neutral and War jumps refused (`jump_rivals_blocked` rose `0->3` host, `0->1` guest, ships stayed in own stars), Ally jump succeeded (`4587573 -> 4325393`), Tribe Rivals guest profile key preserved (`065CBE03`, `!= host 067B3E4A`, `rivals_skipped=2`, `applied=0`, `waits=0`).
  - `rivals_designs_jump_off` (OFF, `SPORE_FIX_OFF=rivals.designs_isolate,rivals.space_jump_ally`): FAIL (76 s) - Neutral jump teleported host to rival star `4325385`, and Tribe Rivals outfit sync overwrote guest profile `065CBE03 -> 067B3E4A` (`applied=1`, `rivals_skipped=0`).
- Wine lab regression run on lane 2 (`out/lab/20261125-174115/summary.md`):
  - `rivals_space`: PASS (66 s).
  - `rivals_tribe`: PASS (36 s).
