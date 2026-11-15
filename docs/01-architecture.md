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

## Threads: who runs what, and how work reaches the main thread (2026-11-15, feat/w51-guards)
<a id="arch-threads"></a>

The rule (1.4): **game objects, the network session and the mod's own game state are touched only on the game's
main thread.** Plan item A14 of 0.7.15 (BUG-1130) made the main thread one id for every module and counts the game calls made
off it.

**The main thread** (the game's own, the one that calls `IDirect3DDevice9::Present`) runs:
* SporeKit's boot and the mod's `sporekit_mod_main` (game::init notes the thread id there);
* `SK_EV_FRAME` callbacks (coop.cpp `on_frame`, inside Present): every module tick, `g_sess.update()` (sends and
  receives), the lifecycle resets, the call-guard reset;
* the task queues of SporeKit (sporekit/console.cpp): `run_on_main` (between frames; a `WM_SK_PUMP` message wakes the
  window procedure, so tasks also run during loading screens) and `run_outside_frame` (`WM_SK_PUMP_OUT`, outside
  Present: UI, fonts, editors, saves, anything that creates D3D resources); console commands with
  `SK_CMD_MAIN_THREAD` (the default) and `SK_CMD_OUTSIDE_FRAME`;
* every game hook the game calls from its own update, and the game's saves (savestrip.cpp's filter acts only there).

`game::on_main()` (game.h) answers "is this the main thread": false before game::init, then compared with the id that
init and every frame note (`stage_epoch_tick` at the top of on_frame, `set_effect_relay_enabled` from effects::tick).
`game::main_thread()` is the id (debug.cpp's `coop.sample` suspends that thread from the console's thread).
The effect hooks, savestrip.cpp and spacecoop.cpp ask it; before A14 each kept its own copy (four ids, the effect
one was 0 until the effects module ran).

**Other threads and what each may touch:**

| Thread | Started by | May touch | Hands work to the main thread by |
|---|---|---|---|
| Keepalive timer (`g_keepalive`, coop.cpp) | `keepalive_start` | only `g_last_frame` and its own flags | `run_outside_frame(keepalive_task)` when no frame came for 0.5 s: the task runs `g_sess.update()` (and the host's world tick) on the main thread, so a loading screen does not drop the session |
| DNS lookup (`spawn_pinned`, coop.cpp; net/session.cpp `g_spawn_thread`) | a join by host name | its own `Lookup` (refcounted, `done` atomic) | the session polls `done` in `update()` |
| Adapters worker (adapters.cpp, BUG-0286) | the co-op window's address list | the OS (GetAdaptersAddresses / sockets) under a fault guard, its result under `g_mu` | the main thread reads the cached list under `g_mu` |
| Auto-update (autoupdate.cpp) | the update check / download | WinHTTP, files in the mod folder, its state under `g_mu` | the main thread polls the state under `g_mu` |
| Memory walk (diag_stats.cpp `mem_walk`) | the support report's memory census | VirtualQuery of the process, its own counters | the main thread reads the result after the thread ended |
| Support report writer (sreport.cpp) | a problem report | files of the report, its job under `g_job_mx` | `g_job_done`, taken by the main thread's tick |
| SporeKit console server (sporekit/console.cpp) | SporeKit boot | runs `SK_CMD_ANY_THREAD` commands inline: they read only atomics or state under a lock (`coop.threads`, `coop.sample`); `SK_CMD_NOT_ON_MAIN` ones wait for frames | queues every other command with `run_on_main` / `run_outside_frame` |
| SporeKit watchdog, crash report, stack-overflow and report-screenshot threads (watchdog.cpp, diag.cpp, reportshot.cpp) | SporeKit | the frame counter, the fault record, files; the crash report reads coop's diag text under `g_diag_mu` (BUG-1022) | nothing (they report) |
| The game's UI skinner render thread (Skinner::cSkinPainterPipeline, Tribe / Civ) | the game | calls our effect hooks: a hook body only calls the original and returns (`if (!game::on_main())`), effects.cpp's ring (`g_qlock`) is filled only from the main thread | nothing |
| The game's planet generation / name threads (Space, BUG-0123) | the game | the seeding hooks keep per-thread state (`t_gen`), nothing else | nothing |
| The game's threads that build audio commands (audio.cpp, tests only: `coop.audio` recording on) | the game | the hooks keep the command being built per thread (`t_cur`) and record it under `g_mu`, then call the original | nothing (`coop.audio` reads under `g_mu`) |
| Wine / DXVK / D3D9 threads | Wine, the driver | never call mod code | nothing |

**Locks of coop:** `g_diag_mu` (coop.cpp, the diag text the crash thread reads), notify.cpp `g_mu` (the notes
queue), modes.cpp `g_world_name_mx` (the planet name), effects.cpp `g_qlock` (the effect ring), adapters.cpp,
autoupdate.cpp and audio.cpp `g_mu`, sreport.cpp `g_job_mx`, mem_census.cpp `g_cs` / `g_track_mu`, game.cpp
`g_off_mu` (the off-main table). Nothing of the session, the stage modules or the lifecycle registry is locked: they
are main-thread only.

**The off-main counter.** `skg::call_probe` (sdk/include/sporekit_game.hpp) is called by every skg helper
(`thiscall`, `cdecl_call`, `stdcall_call`, `vcall`) with the game function's address; coop's game.cpp installs one
that, off the main thread, counts the call per call site (its return address in coop.dll) and game function
(mods/coop/off_main.h, unit test `test_off_main` in tests/npc_tests.cpp) and writes one log line per new site:
`threads: game call 0x... from coop.dll+0x... on thread N, not the main thread M (A14, coop.threads)`. `coop.threads
[reset|on|off]` prints the main thread id and the table. Calls through the `o_*` trampolines (a hook calling the
game's original on the game's own thread) and through typedef'd function pointers are not counted. Any count above
0 is a bug: a hook the game calls from another thread that does game work, or an any-thread command that calls the
game.
