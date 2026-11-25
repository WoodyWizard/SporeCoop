---
id: BUG-1566
title: Rivals: isolate drama events and avoid unnecessary guest world reloads on host Creature Editor
status: fixed
kind: bug
stage: Creature
reported: 2026-11-25
source: 0.8.1 Rivals isolation audit
role: both
versions: 0.8.0.2
fixed_in: 0.8.1
branch: w081/rivals-editor-drama
commits: b62f88ed
tests: rivals_editor_drama, rivals_editor_drama_off
signature:
docs: docs/11-rivals.md
related: BUG-0486, BUG-1547, BUG-1551
---

## Report
1. `mods/coop/drama.cpp` had zero Rivals awareness:
   - In Creature Rivals, a rival guest triggering Summon Flock (`K_FLOCK` -> `drama::summon_for(puppet)`) spawned creatures on the host that joined the **host's pack** (taking host pack slots and fighting on the host's side, while hijacking the host's single `cSummonCreatureEvent` avatar swap).
   - Host-centered meteor (`kind 3`) and rain (`kind 5`) events relayed via `K_DRAMA` onto rival guests at distant nests across the planet.
   - In Tribe Rivals, host `creature_attack` (`kind 7`) wild animal raids could target NPC tribes taken over by rival guests.
2. In Creature Rivals, when the host entered the Creature Editor to evolve their own species (especially on brain growth / nest migration `CRG_SpeciesMigrate`, or when the 30 s `evo_check_until` fallback fired), the host queued a full `MSG_WORLD` save/share that kicked every rival guest out to the galaxy menu, forced a full world reload, and wiped the guest's independent DNA progress (e.g. `175 -> 225`), even though each rival has their own species, nest, and evolution bar and `creations.cpp` already syncs the host's newly evolved species live. Additionally, `host_busy.cpp` only checked `spaceext::rivals()` instead of `rivals::active_in_mode(g_mode)`, pausing non-Space Rivals guests on host pause/caption states.

## Root cause
- `mods/coop/drama.cpp` relayed host meteor/rain events (`kRelay`), accepted guest `K_FLOCK` via `drama::summon_for(puppet)`, and allowed Tribe `creature_attack` raids unconditionally without checking `creature_rivals::active()` or `tribe_rivals::active()`.
- `mods/coop/world.cpp` queued full post-editor `MSG_WORLD` shares in `share_with()` during Creature/Cell Rivals even when the host returned from a same-stage editor on the same planet for an already-active rival peer.
- `mods/coop/creature_rivals.cpp` did not update `g_nests[0]` when host species or nest position changed in place after an editor/migration, and did not re-attach existing guest herds by nest position before re-picking.
- `mods/coop/host_busy.cpp` passed only `spaceext::rivals()` to `busy_model::act()` instead of `spaceext::rivals() || rivals::active_in_mode(g_mode)`.

## Fix
- Added portable policy helpers `evaluate_rivals_drama()` and `should_skip_editor_world_share()` in `mods/coop/creature_rivals_logic.h` with ASan/UBSan unit tests in `tests/creature_rivals_tests.cpp`.
- `COOP_FIX("rivals.drama_isolate", "BUG-1566")` in `mods/coop/drama.cpp`:
  - Isolates rival guest Summon Flock (`drama::summon_for` and local guest `kind 8`) in Creature Rivals so rival flocks never join the host's pack (`rivals_flock_isolated`, `rivals_blocked`), and clears any stale `g_flock_event` in `h_SummonUpdate`.
  - Stops host meteor (`kind 3`) and rain (`kind 5`) `K_DRAMA` relay TX/RX in Creature Rivals so host-centered events stay host-local (`rivals_blocked`).
  - Blocks `creature_attack` (`kind 7`) in Tribe Rivals (clearing any player-owned target tribe on host) so converted rival villages are not unfairly raided.
  - Reports `rivals_isolate`, `rivals_blocked`, and `rivals_flock_isolated` in `coop.drama`.
- `COOP_FIX("rivals.editor_no_reload", "BUG-1566")` in `mods/coop/world.cpp` + `mods/coop/creature_rivals.cpp` + `mods/coop/host_busy.cpp`:
  - Tracks host same-stage Creature/Cell Editor returns (keeping the window active across post-editor migration cinematics) and skips redundant post-editor `MSG_WORLD` shares in `world::share_with()` for already-active rival peers (`editor_share_skipped` in `coop.status`), while NEVER skipping manual shares (`tag == 0`), first-join shares, stage transitions (`Cell -> Creature`, `Creature -> Tribe`), planet switches, or menu/other-stage recovery shares (`creature_rivals::peer_recently_active`).
  - Updates host home nest species/position on in-place evolution/migration in `creature_rivals.cpp` and re-attaches existing guest herds by nest position so host editor/migration never moves a rival's nest.
  - Updates `host_busy.cpp` to pass `spaceext::rivals() || rivals::active_in_mode(g_mode)` to `busy_model::act()` so rival guests in all Rivals stages do not pause for host pauses/captions (while keeping `core.editor_long_wait` timeout extension active while host loads an editor).

## Verification
- Native unit tests & linters: `make -j$(nproc)`, `make unit-cpp unit-net unit-lint`, `python3 tools/tests/test_lint.py` all passed (0 problems).
- Real two-game Wine lab lane 6 (`out/lab/20261125-174244`):
  - `rivals_editor_drama` (ON): **OK** (125 s, 0 host faults, 0 guest faults). Guest Summon Flock isolated (`rivals_flock_isolated=1`, `relayed=0`, host pack `0 -> 0`); host meteor/rain ran locally without relaying (`relayed_tx=0`, `rivals_blocked 1 -> 4`, guest untouched); host DNA +250 -> Creature Editor evolution (`065CBE70 -> 2931361B`) + `CRG_SpeciesMigrate` skipped post-editor share (`editor_share_skipped 0 -> 1`, `world_offers_sent 1 -> 1`); guest stayed in Creature stage (`world_offers_recv 1 -> 1`, `loads 2 -> 2`, `left=[]`), kept own rival nest `(-125.8 -488.1 -64.8)` and own DNA (`175`), and saw host's evolved puppet live.
  - `rivals_editor_drama_off` (`SPORE_FIX_OFF=rivals.drama_isolate,rivals.editor_no_reload`): **FAIL** as expected (124 s). Guest Summon Flock added 3 creatures to host pack (`relayed=1`); host meteor & rain relayed onto guest (`relayed_rx=1, 2`, `active=1`); host post-editor migration sent `MSG_WORLD_OFFER` (`1 -> 2`), kicked guest to `GalaxyGameEntry` (`loads 2 -> 4`), and overwrote guest DNA `175 -> 225`.
- Regression suite on lane 6 (`out/lab/20261125-174729`): `rivals_creature` **OK** (39 s) and `rivals_core` **OK** (36 s), 2/2 PASSED.
