---
id: BUG-1558
title: Default Settings presets for Rivals vs Together mode and live Friendly Fire toggle
status: fixed
kind: feature
stage: All
reported: 2026-11-25
source: Discord (@Хлебушек)
role: both
versions: 0.8.0.1
fixed_in: 0.8.0.2
branch: w0802/rivals-presets
commits: c516fe12, 136b3aed
tests: rivals_presets, rivals_presets_off
signature:
docs: docs/11-rivals.md, docs/07-modes.md
related: BUG-0768, BUG-0794
---

## Report
1. Selecting Rivals mode (`All rivals`) vs normal co-op (`All together`) leaves all Settings on their co-op defaults instead of applying sensible mode defaults (such as enabling `all.friendly_fire`, setting full solo DNA pace, per-player Civ money, etc. for Rivals and restoring Together defaults when switching back).
2. Players already inside a world cannot enable `all.friendly_fire` because it is marked `Scope::Session` ("change in main menu before loading"), even though every gameplay reader reads `settings::on(kFriendlyFire)` live every hit/frame.

## Root cause
- `rivals::set_preset()` updated per-stage Rivals flags (`rivals::State::config`) without applying a matching default Settings preset in `settings`.
- `kDefs[kFriendlyFire]` in `mods/coop/settings_core.h` was declared `Scope::Session`, blocking live host toggles once a world was open even though all six readers (`cellsync_combat.cpp`, `civ.cpp`, `coop_cmds.cpp`, `npc.cpp`, `puppets.cpp`, `tribe.cpp`) poll `settings::on(settings::kFriendlyFire)` dynamically without caching.

## Fix
- Defined portable `settings::rivals_preset_value(Id)` and `settings::apply_mode_preset_values(Values&, const Values&, bool)` in `mods/coop/settings_core.h`:
  - Rivals preset enables `all.friendly_fire`, `civ.own_money`, and `space.rivals`, disables `all.dna_pace`, `core.switch_vote`, `creature.mode`, and co-op teamplay bonuses (`cell.regen`, `all.partner_arrows`, `cell.knockout`, `cell.respawn_small`, `creature.pack_rescue`, `creature.revive`, `creature.pack_heal`, `creature.joint_hunt`, `tribe.pincer`, `tribe.chief_aura`, `tribe.duet`, `tribe.epic`, `civ.roles`, `civ.joint_siege`, `civ.superweapon_vote`, `space.disable`, `space.contracts`, `space.diplomacy`, `space.allies`, `space.finale`, `adventure.shared`), while preserving user/lobby settings (`all.max_players`, `all.autosave`, `all.name_tags`, `all.late_join`, `all.partner_map`, `all.shared_dna`, `creature.npcs_target_partners`, `all.party_difficulty`, `cell.lineage_cells`, `core.editor_now`, `space.trade`).
  - Together preset restores all preset-managed keys to `g_defaults` (built-in + `coop.ini` overrides) while preserving user/lobby settings.
- Added `settings::apply_mode_preset(bool)`, `settings::is_live(Id)`, `coop.settings preset rivals|together`, and `COOP_FIX("rivals.settings_preset", "BUG-1558")` in `mods/coop/settings.cpp` (consolidating bulk guest feed notices so switching presets posts one concise summary instead of 25+ lines).
- Wired `rivals::set_preset()` in `mods/coop/rivals.cpp` to call `settings::apply_mode_preset()` on `AllRivals` and `AllTogether` (never `Custom`), and added hover tooltips on the F9 Rivals preset button in `mods/coop/coop_ui.cpp`.
- Promoted `kDefs[kFriendlyFire].scope` from `Scope::Session` to `Scope::Live` (gated by `rivals.settings_preset` so `SPORE_FIX_OFF=rivals.settings_preset` reproduces both the no-op preset switch and the session-locked refusal of 0.8.0.1), updated `tests/validate_tests.cpp`, updated `tools/tests/coop_settings_editor_lock.py` to test a remaining Session setting (`all.dna_pace`), and refreshed `tools/tests/golden/settings_{menu,host,guest}.png` for the live `Friendly fire` row.

## Verification
- Unit tests & linters: `make -j$(nproc)`, `make unit-cpp unit-net unit-lint`, `python3 tools/tests/test_lint.py` all passed (0 problems).
- Two-game Wine lab verification on lane 3:
  - `out/lab/20261125-144946`: `rivals_presets` **PASS** (29 s, 0 faults); `rivals_presets_off` (`SPORE_FIX_OFF=rivals.settings_preset`) **FAIL** (71 s, 0 faults; confirmed preset switch leaves all 26 settings untouched and live `all.friendly_fire` toggle is refused as Session).
  - `out/lab/20261125-145140`: `rivals_core` **PASS** (35 s), `rivals_creature` **PASS** (39 s).
  - `out/lab/20261125-145720`: `ui_golden` **PASS** (86 s, 12/12 steps), `rivals_cell` **PASS** (71 s), `settings_editor_lock` **PASS** (39 s).
