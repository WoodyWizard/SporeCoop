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

## Space kept state: one store for a rejoiner's colonies, hold, badges and budget (2026-11-15, feat/w51-space)
<a id="arch-space-kept-store"></a>

C5 of docs/plan-0.7.15.md (audit NET-1, BUG-1132). Every player flies in a copy of the host's save, so what a guest
owns only in its own game is gone when it rejoins and loads the host's world again. Four features keep it on the host
and give it back: space_map (own colonies, BUG-0195), space_trade (the cargo hold, BUG-0678), space_badges (badges and
rank, BUG-0786), space_rivalry (the Rivals budget, BUG-1029). Up to 0.7.14 each had its own table, serial, timeout and
restore rule; they now share `mods/coop/space_kept.h` (portable, tests/space_tests.cpp):

* `spacekept::KeptStore<T>` (host): player key (`spaceext::kept_key`: the rejoin token, BUG-1107) -> Entry {value,
  have, name, id, restoring, serial, sent}. `world(epoch)` empties it in another Space world; `bind(key, id, name,
  exclusive)` ties an entry to the player's session id and says whether this is its first report since the join
  (exclusive: space_map, an entry of another key on the same id lets go); `begin()` starts a restore (serial),
  `keep()` takes a report as the player's state unless a restore runs, `done(id, serial)` ends it, `tick(now, here,
  resend)` unbinds a player who left and sends again after 10 s without an answer, `leave(id)` (space_map's
  PLAYER_STATE, BUG-0810) and `session_end()` unbind; `find` / `put` are the keyed values Rivals uses.
* `spacekept::RestoreWindow` (guest): the Space world of the join (its own world before the host's arrived), the world
  whose first report went as fresh, the world the kept copy was written into, the world the host said it keeps
  nothing for, the Rivals budget known; `open()` / `pending()` are the window in which the kept copy may still
  replace the state (BUG-1034's "restore pending", at most 20 s after the fresh report).
* `spacekept::HeldQueue`: what arrives in that window and would go into the state about to be replaced (space_trade's
  gifts and sales, BUG-1034), handled once it closes; 64 at most.

The handshake on the wire stays each feature's own (no wire change; wire/space_map.h, space_trade.h, space_badges.h,
space_rivalry.h and their golden bytes in tests/wire_tests.cpp): report -> kept copy (K_KEPT, K_INV_KEPT, K_BADGES_KEPT)
-> answer (K_KEPT_DONE, K_INV_DONE, K_BADGES_DONE); Rivals asks instead (K_ASK -> K_SET, repeated by the guest every
2 s), so it uses only the keyed values and the window's known budget. The features' own fix switches
(space.keep_colonies, space.kept_leave, space.keep_items*, space.keep_badges, space.kept_token, space.merge_known_budget,
space.trade_wait_restore, ...) keep working on top of the store. Not moved: Space still resets through
`spaceext::Feature::on_session_end` and `world_epoch`, not lifecycle.h (that is C2).

