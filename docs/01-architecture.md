# 1. Architecture and loading into the game

## 1.1 Goals

* The mod is **injected** into the original `SporeApp.exe`; nothing in the exe is patched on disk.
* Game functions are called through a **symbol database** (data), not addresses hardcoded in code:
  supporting another exe version = a new `.sym` file.
* Mods are separate DLLs and can be **reloaded without restarting the game**.
* Everything is controlled and checked from outside (a TCP console), for fast tests and RE.

## 1.2 Layers

```
 SporeApp.exe (MSVC 2008, 32-bit, no RTTI)
   │ imports steam_api.dll (GOG Steam API shim, 3 functions)
   ▼
 steam_api.dll  ← OUR proxy loader (loader/)
   │  • forwards SteamAPI_* to steam_api_orig.dll (the GOG original)
   │  • hooks the exe entry point → loads sporekit.dll after all DLLs are initialized
   ▼
 sporekit.dll  (sporekit/)
   │  • log, config, symbol database (data/symbols/*.sym), address rebasing
   │  • hook engine (MinHook), game lifecycle hooks and D3D9 Present
   │  • main-thread task queue, TCP console (devkit), commands
   │  • mod manager: mods/*.dll, hot reload
   ▼
 mods/coop.dll, mods/<others>.dll   - C ABI (sdk/include/sporekit.h)
```

## 1.3 Why a `steam_api.dll` proxy

Loading options considered:

| Option | Pros | Cons | Decision |
|---|---|---|---|
| `steam_api.dll` proxy | the DLL sits in the game folder, Wine loads it first without overrides; the exe imports only 3 functions | GOG-specific (the Disk version doesn't import steam_api) | **chosen** |
| `dinput8.dll` / `winmm.dll` proxy | universal across all versions | Wine needs `WINEDLLOVERRIDES=dinput8=n,b` | fallback, same loader code |
| ModAPI Launcher Kit | ecosystem | .NET under Wine, **this exe version is not supported** | no |
| Injection via `CreateRemoteThread` | no files in the game folder | needs its own launcher, startup races | no |

The loader itself is thin: its job is to wait for a safe moment and load
`sporekit.dll`. The safe moment is the **exe entry point** (`AddressOfEntryPoint`): all DLLs
are already initialized, the loader lock is not held, and the game hasn't started running yet. The loader
temporarily replaces the first 5 bytes of the entry point with a `jmp` to its stub; the stub loads SporeKit,
restores the bytes and jumps to the original entry.

## 1.4 Lifecycle

| Event | Source | What mods use it for |
|---|---|---|
| `on_load` | right after the mod's LoadLibrary | registering commands, hooks |
| `on_app_preinit` | hook on `App::cAppSystem::PreInit` (after) | early setup |
| `on_app_init` | hook on `App::cAppSystem::Init` (after) | game managers are created: cheats, messages |
| `on_post_init` | hook on `App::cSporeApp::Init` (after) | everything is ready, main menu |
| `on_frame(dt)` | hook on `IDirect3DDevice9::Present` | all mod game logic, networking, overlay |
| `on_shutdown` | hook on `App::cSporeApp::Shutdown` | save state |
| `on_unload` | before FreeLibrary (hot reload) | remove hooks/objects |

Game objects are accessed **only on the main thread** (in `on_frame` or
in console commands, which SporeKit runs on the main thread).

## 1.5 Symbol database

Files `mods/sporekit/symbols/<name>_<first 8 hex chars of the exe SHA-256>.sym` (in git: `data/symbols/`;
the supported build is GOG 2024, `march2017_dc04aee5.sym` and `overrides_dc04aee5.sym`):

```
# exe_sha256=dc04aee5...
App::cCheatManager::AddCheat=0x0067FC10 ; high m17=0x0067FC10
```

* On startup SporeKit computes the exe's SHA-256 and picks the file; if there is no file,
  mods that require symbols are disabled (instead of crashing the game).
* Addresses are given for base `0x400000`; under ASLR they are rebased (the exe has `DYNAMIC_BASE` set).
* The confidence: `high` / `medium` / `low` in the generated file; every function the mod calls has
  an entry in `data/symbols/overrides_dc04aee5.sym` (it wins over the generated one) with
  `verified` / `likely` / `unknown` and its signature (CLAUDE.md 1, `tools/re_lint.py`).
* A known build that is not supported (GOG 2016, `922af7c7`) loads no symbols and no mod; the log and
  the overlay say "this game version (GOG 2016) is not supported" (BUG-1125).
* A mod declares the symbols it needs as a list; if any required one is missing, the mod reports
  it to the log and console and doesn't enable the corresponding functionality.

## 1.6 File layout in the game

```
SporebinEP1/
  SporeApp.exe              (untouched)
  steam_api.dll             ← our loader
  steam_api_orig.dll        ← GOG original (renamed on install)
  sporekit.dll
  mods/
    sporekit.ini            config (console port, mod list, log)
    sporekit/symbols/*.sym
    coop.dll
    coop.ini
    logs/sporekit.log
    .shadow/                shadow copies of DLLs for hot reload
```

`tools/spore.sh install|uninstall` applies/reverts the steam_api.dll swap (with a backup).

## 1.7 Content (for the future)

Spore picks up every `.package` (DBPF) in `Data/` on its own. If the mod ever needs its own
UI layouts (`.spui`), localization or prop files, they go into a separate package
`Data/SporeCoop.package`; DLL code and content are decoupled.

## The lifecycle registry: session and world state (2026-11-13, fix/w49-life)
<a id="arch-lifecycle-registry"></a>

BUG-1055 (owner decision 3 of docs/plan-0.7.14.md): module state that belongs to one session or one world is
registered in one place, `mods/coop/lifecycle.h`, next to its definition, so a module added later cannot miss the
reset (the class of BUG-1051, BUG-1054, BUG-1002, BUG-1049; the per-player twin is `player_life.h`, BUG-0422).

* `SESSION_STATE(name, fix, reset, held)`: reset at every session end (coop.cpp `end_session`, block 3: a Leave, a
  drop, a new host or join, the unload).
* `WORLD_STATE(name, fix, reset, held)`: reset when the game's world changes: the sum of the stage counters of
  `stage_epoch.h` moved (a load, a received world, another stage; an editor visit and the galaxy menu are no new
  world). Checked once a frame right after `game::stage_epoch_tick`, before any message or module tick.
* `LIFECYCLE_STATE(name, lifecycle::kSession | lifecycle::kWorld, fix, reset, held)`: both.
* `fix`: the entry's own `coop.fix` switch (one bug per switch) or null; `core.lifecycle_reset` (BUG-1055) switches
  every entry off at once. `reset` is mod state only (no game call, no send, no wait: it can run inside
  `g_sess.update()`) and idempotent; `held` counts what is held.
* `coop.lifecycle` lists the entries; `coop.lifecycle held` prints what is held now and what the last reset of each
  scope left (`left session: none (resets N)`, `left world: none (resets N)`): a test reads the record of the reset,
  not the state the next world fills again (tools/tests/coop_lifecycle_reset.py).
* A module keeps its own `on_session_end` / `on_world_change` for work that calls the game (despawning mirrors);
  plain clears go into the registry. Entries of 0.7.14: tribe.cycle, tribe.ring, tribe.world_map, civ.cycle,
  world.host_reload (world only: a session end in the live Tribe world keeps BUG-0332's reload due), cell.body and
  cell.own_good (world only), cell.food_queue, cell.join, game.air, parts.cell_queue, mirror_fx.keys, nest.own,
  social.meter_tasks (the close of BUG-0808 stays queued), social.pending, creature_hp.puppets, drama.last_events.

## The coop core files, the start and the frame (2026-11-15, feat/w51-split)
<a id="arch-start-and-frame"></a>

B2 of 0.7.15 (BUG-1128, audit CORE-2 / CORE-8): `mods/coop/coop.cpp` (8.6k lines) was split by pure moves; no
behaviour changed (the proof: docs/10-dev-pipeline.md#pipeline-refactor-proof).

| File | What it holds |
|---|---|
| coop.cpp | `COOP_VERSION` (the tools read it here), the session object and the partners' table (`g_sess`, `g_remotes`), feed / chat / notices, the host's world state for the modules (own_world, host_world_now, the host-away state), keepalive and diagnostics, the frame (`on_frame` and its phases), marks, tags and the overlay, the UI glue, `support_init`, `sporekit_mod_main` and its phases |
| coop_cmds.cpp | every console command of the core (`c_*` and the registrations, `register_commands_*`), except coop.track / coop.bodyrec (next to their recorders) |
| dna.cpp | DNA routing: the totals' state, the cause ledger's notices, the single-player pace, `on_dna`, the solo DNA's await, a guest's own spends (the portable rules: dna_route.h, dna_ledger.h, dna_pace.h) |
| vote.cpp | the evolution vote and action votes, the switch vote, the offers, the guest's own editor, the editor hooks, `game_busy` / `save_blocker`, the editor-pending flag |
| puppets.cpp | the partners' puppets (make, drive, despawn, forget, strays), the local state that is sent, partner pass-through, animations, the recorders (coop.track, coop.bodyrec, the monitor's fields, the support trace) |
| session_glue.cpp | the session callbacks (`setup_callbacks`, the message dispatch), `end_session`, host / join / leave, roles and ready flags, the host's world for its guests (on_mode_change, follow_phase, share retries and waits, the evolution in place, the lobby) |
| coop_state.h | what more than one of these files uses: the shared types, constants and one declaration per shared global or function, grouped by the file that defines it. Only these six files include it |

Rules: the six files are one namespace, `coop_core` (it was coop.cpp's anonymous namespace; coop_state.h has the
using-directive an anonymous namespace has). What only one file uses stays `static` there; a new shared name gets
its declaration in coop_state.h under its file. Static initialisation does not cross files: the registrations
(`COOP_FIX`, `CALL_GUARD`, `PLAYER_STATE`) only store pointers, and their order across files (the link order) is
used by nothing; `g_sess` and `g_remotes` stay defined next to each other in coop.cpp.

### The start: sporekit_mod_main

`game::init` (or no mod), then in this order (each phase a function in coop.cpp; the command registrations sit
where they were, SporeKit keeps the commands by name):

| # | Phase | What |
|---|---|---|
| 1 | init_config | coop.ini: name, share switches, the support trace |
| 2 | init_world_share | world.cpp's transfer and its probes, shared resources, the game's animation / DNA listeners |
| 3 | init_player_modules | npc, creature_hp, player_rules, mirror_fx, effects, nest, fields, wsync, worldobj, realign, audio, parts, savestrip, views, settings, find, portrait, sreport, social, the damage filter |
| 4 | init_session | support_init, setup_callbacks, the frame / fault / overlay subscriptions, the keepalive thread |
| 5 | register_commands_core, coop_register_debug_commands | |
| 6 | init_creature_hooks | teamplay, the creature and editor hooks (SelectUI, collision / hover filters, StageMsg, Submit, CellToCreature, CreatureGame / Simulator messages, BlockingErrors) |
| 7 | register_commands_editor | |
| 8 | init_tribe_and_creations | tribe, drama, notify, creations, the puppets' models |
| 9 | register_commands_civ | |
| 10 | init_cell | cellsync |
| 11 | register_commands_cell | |
| 12 | init_civ_and_space | civ, census, squadron, spacecoop, the start-up switches (fixes_off.txt), gamefixes, autoupdate |
| 13 | register_commands_stages, init_ui, adapters::init, register_commands_ui, the cheat | |

### Order of a frame: on_frame

The frame-wide scopes (`modes::CellScope`, BUG-0271; `game::LiveScope`, BUG-0426) and `now` are on_frame's; the
phases run in this order, and the order is part of the fixes named:

| # | Phase | Contract |
|---|---|---|
| 1 | frame_observe | before any message: frame timing and the session clock (support::tick first), the stage counters (`game::stage_epoch_tick`, BUG-0217: before any message or module tick), the Tribe / Civ world checks, the world resets (`lifecycle_world_tick`, BUG-1055), the puppets forgotten when the world was torn down (`avatar_change_check`, BUG-0091), the puppets / proxies / bodies read as drawn (monitor, bodyrec) |
| 2 | frame_receive | `g_sess.update()`: every message of this update; the host's WELCOME world and every handler see the last frame's `g_mode` / `g_world` / `g_local` (BUG-0231); then the partner pass-through list and the lobby |
| 3 | frame_votes_and_ui | the guest's latency for the drive (BUG-0425, BUG-0651), votes, the own editor, offers, the co-op window, the menu |
| 4 | frame_world_ticks | the host's world share tick (a guest: a received world waiting), the save strip, npc, player_rules, nest, fields (after the modules that make mirrors), wsync, worldobj, realign, parts, effects, social, the evolution keys, creature_hp, shared resources |
| 5 | frame_send_totals | host: the DNA total (on a change, every 2 s) and the time of day (every 1 s) |
| 6 | frame_local_update | this frame's `g_mode` (on_mode_change), `g_adapter`, `g_world`, `g_local`; settings, the HUD helpers, the probes, the second avatar check, join apart (BUG-0989) |
| 7 | frame_stage_ticks | strays, follow_phase, the share wait, host away, DNA spends and notices, Tribe, Cell, Civ, the squadron, the updater, Space, teamplay |
| 8 | frame_send_state | our player state (`net::state_due`: every 0.05 s, a stop at once, BUG-1093) and the echo test remote |
| 9 | frame_drive | every partner's puppet driven (forgotten without a local entity), then coop.track and the support trace |

The keepalive path (a modal dialog stops the frames, the window still pumps messages; `keepalive_task`, outside the
frame) runs only `g_sess.update()` and, on the host, `world::tick`: none of phases 1 and 3-9 (no stage counters, no
world resets, no puppet checks) before those messages. Open question of the audit (CORE-8): no handler is known to
need them.
