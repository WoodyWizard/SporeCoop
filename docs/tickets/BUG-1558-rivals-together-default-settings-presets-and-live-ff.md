---
id: BUG-1558
title: Default Settings presets for Rivals vs Together mode and live Friendly Fire toggle
status: open
kind: feature
stage: All
reported: 2026-11-25
source: Discord (@Хлебушек)
role: both
versions: 0.8.0.1
fixed_in:
branch: w0802/rivals-presets
commits:
tests: rivals_presets, rivals_presets_off
signature:
docs: docs/11-rivals.md, docs/07-modes.md
related: BUG-0768, BUG-0794
---

## Report
1. Selecting Rivals mode (`All rivals`) vs normal co-op (`All together`) leaves all Settings on their co-op defaults instead of applying sensible mode defaults (such as enabling `all.friendly_fire`, setting full solo DNA pace, per-player Civ money, etc. for Rivals and restoring Together defaults when switching back).
2. Players already inside a world cannot enable `all.friendly_fire` because it is marked `Scope::Session` ("change in main menu before loading"), even though every gameplay reader reads `settings::on(kFriendlyFire)` live every hit/frame.

## Root cause
- `rivals::set_preset()` updates per-stage Rivals flags (`rivals::State::config`) without applying a matching default Settings preset in `settings`.
- `kDefs[kFriendlyFire]` in `mods/coop/settings_core.h` is declared `Scope::Session`, blocking live host toggles once a world is open.

## Fix
- Define portable Rivals vs Together default Settings presets in `mods/coop/settings_core.h` and expose `settings::apply_mode_preset(bool rivals_mode)` (gated by `COOP_FIX("rivals.settings_preset", "BUG-1558")`).
- Call `settings::apply_mode_preset(...)` from `rivals::set_preset()` on the host when switching between `AllRivals` and `AllTogether`, broadcasting updated settings to guests and persisting cleanly.
- Promote `all.friendly_fire` from `Scope::Session` to `Scope::Live` (all readers already poll `settings::on(kFriendlyFire)` dynamically; update `tests/validate_tests.cpp` and verify no cached state depends on session lock).

## Verification
Pending implementation and Wine lab verification (`rivals_presets` PASS + `rivals_presets_off` FAIL).
