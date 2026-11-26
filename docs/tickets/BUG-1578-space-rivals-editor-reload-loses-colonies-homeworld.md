---
id: BUG-1578
title: Space / Space Rivals: guest loses colonies and homeworld resets when host uses an editor and leaves the planet (GitHub #223)
status: fixed
kind: bug
stage: Space
reported: 2026-11-26
source: GitHub #223 (Akasaka-boi)
role: both
versions: 0.8.1.1
fixed_in: 0.8.1.3
branch: main
commits: 8021d561
tests: space_editor_colonies, space_editor_no_reload_off, space_keep_colonies_reload_off, core_tests, space_tests
signature:
docs: docs/11-rivals.md
related: BUG-0195, BUG-1151, BUG-1550, BUG-1562, BUG-1566
---

## Report
GitHub #223 (`Akasaka-boi`, Space Rivals on `0.8.1.1`): whenever the host opened an editor in Space (`8707BE7D`, `4E3F7777`) and later left the planet, the guest was forced to reload the host's world (`Game1 -> Game2 -> Game3`), losing all founded colonies and resetting their emigrated homeworld (`Малий -> Габара`) while budget, cargo hold, and badges restored.

## Root cause
Three interacting bugs confirmed in `sporekit.log` and `report-20261127-000411.txt`:
1. **Unnecessary post-editor full-galaxy world reload**: `session_glue.cpp:on_mode_change()` set `r.needs_world = true` for all guests whenever the host returned from any Space editor. `evolve_inplace_ok()` returned `false` because host and guest are on different stars/planets (`ls.world != world`), and `rivals.editor_no_reload` (`BUG-1566`) only covered Cell and Creature. The queued Space save waited until the host left the planet and then forced every guest through Galaxy Menu into a fresh copy of the host's galaxy.
2. **Kept colonies wiped on in-session reload without disconnect**: unlike cargo hold (`space_trade.cpp`), badges (`space_badges.cpp`), and budget (`space_rivalry.cpp`), `space_map.cpp:kept_roster()` only triggered `send_kept()` when `g_store.bind()` reported `first == true` (only after `PLAYER_LEAVE`). During a mid-session `MSG_WORLD` reload the guest kept `id=1`, so `first` was `false`, the host never sent `K_KEPT`, and the guest's fresh empty roster overwrote `g_store` with `[]`.
3. **Space Rivals homeworld cities excluded & solar-system `"wait"` deadlock**: `space_map.cpp` skipped `c.home` when tracking kept colonies and `K_COLONY` cities (assuming Together mode's shared homeworld), and `space_allies.cpp:colonize()` / `cities_apply()` checked `active_planet_record() == rec` without `context() == 0`, stalling homeworld city restore in `"wait"` while in solar system view.

## Fix
- **Post-editor Space/Rivals no-reload (`mods/coop/share_logic.h`, `mods/coop/session_glue.cpp`, `mods/coop/world.cpp`)**: added `COOP_FIX("space.editor_no_reload", "BUG-1578")` (default ON) skipping post-editor full-galaxy `MSG_WORLD` reloads in Space (both Together and Rivals) via `share_logic::skip_space_editor_reload()`, and extended `rivals.editor_no_reload` tracking to Tribe and Civ Rivals as well. Also gated the `BUG-0941` `"host loaded another Space save"` log on `Role::Host`.
- **In-session kept-colony restore & Space Rivals homeworld cities (`mods/coop/space_kept.h`, `mods/coop/spacecoop.h`, `mods/coop/coop.cpp`, `mods/coop/space_map.cpp`, `mods/coop/space_allies.cpp`)**: added `COOP_FIX("space.keep_colonies_reload", "BUG-1578")` (default ON):
  - Host monitors guest mode via `spacecoop::Ctx::peer_mode` and calls `g_store.leave(id)` whenever `spacekept::mode_tears_down_space(mode)` (Galaxy menu / Loading / other stage, never editors/adventures), plus a `world::share_serial(id)` fast-reload fallback so `K_KEPT` + `K_KEPT_COLONY` are sent instead of wiping `g_store`.
  - Space Rivals includes the guest's emigrated homeworld (`c.home` when `own_colony(id, c)`) in kept colonies/cities and waits for `!spacerivals::emigration_in_progress()` before applying restore.
  - Scoped `space_allies.cpp` `colonize()` and `cities_apply()` active-planet `"wait"` guards to `context() == 0` (landed surface only), matching all other `space_allies.cpp` checks.

## Verification
- Unit tests (`tests/core_tests.cpp`, `tests/space_tests.cpp`): `skip_space_editor_reload`, `mode_tears_down_space`, and in-session `KeptStore` reload handshake PASS.
- Two-game Wine lab ON + OFF twins (`out/lab/20261126-142431`, `coop_space_editor_colonies.py`):
  - ON (`space_editor_colonies`): **PASS** (50 s; Part A: host opened/exited real Space House editor `4E3F7777` -> `space_skipped 0 -> 1`, 0 world offers sent, guest epoch unchanged; Part B: manual `coop.share` in-session reload `epoch 1 -> 3` -> host `reloads=1 sent=1 done=1 cities back=2`, guest restored both colony `01420011` and homeworld `00420011` with `restored=2 failed=0 pending=0`, 0 faults).
  - OFF-A (`space_editor_no_reload_off`, `SPORE_FIX_OFF=space.editor_no_reload`): **FAIL** (43 s; host sent `MSG_WORLD` after editor `4E3F7777`, forcing guest reload `epoch 1 -> 3`).
  - OFF-B (`space_keep_colonies_reload_off`, `SPORE_FIX_OFF=space.keep_colonies_reload`): **FAIL** (140 s; in-session `coop.share` wiped host kept colonies to `0` and guest lost `01420011` back to `tech=1`).
- Regressions (`coop_space_colony_rejoin.py`, `coop_rivals_editor_drama.py`): **PASS** (0 faults).
