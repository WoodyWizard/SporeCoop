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

## Wave C of 0.7.15: the shared mechanisms and how a stage adopts them (2026-11-15, feat/w51-ccore)
<a id="arch-wave-c"></a>

Plan items C1-C4 of docs/plan-0.7.15.md (audit docs/audit-0.7.15/stages.md STG-1..4, core.md CORE-3 / CORE-4).
Rule "unit first": each mechanism is a portable header with unit tests (tests/core_tests.cpp, in `make test` under
ASan / UBSan and TSan) before any stage uses it. This branch adds the headers only; no stage file changed. Each stage
then adopts them one at a time, behind one switch per stage and mechanism, runs its existing lab tests with the switch
on and off, and deletes the old path (and the switch) in the same release (plan rule "a shared mechanism replaces a
stage path behind a switch"). Tickets: BUG-1140 (C1), BUG-1141 (C2), BUG-1142 (C3), BUG-1143 (C4); they stay open until
the stage adoptions land.

Common recipe for a stage agent (each mechanism below adds its own steps):
1. A `COOP_FIX("<stage>.<mechanism>", "<ticket>", g_x, "...")` switch, on by default; the old code stays in the
   `else` branch untouched.
2. The new path only through the header's API; the stage keeps its game calls (spawn, RemoveMember, kill_local,
   despawn) in the callbacks it hands to the header.
3. Proof: `make test`; the stage's lab tests named below with the switch on (PASS) and off (PASS: refactoring, same
   behaviour), run dirs in the ticket; for C3, a behaviour change, the new class test FAILS with the switch off.
4. Delete: the old branch, its globals, its LIFECYCLE_STATE entries and the switch, in one commit after the PASS runs;
   `tools/ratchet_lint.py --lower` when an `object_alive(` count went down.

## The mirror registry: ids and game objects (2026-11-15, feat/w51-ccore)
<a id="arch-mirror-registry"></a>

C1, BUG-1140. `mods/coop/mirror_registry.h`, `mirror::Registry<Id, Data>`: one entry per net id with the object bound
to it (`mirror::Ref{p, serial, noun}`, the fields of `game::LiveRef`, or none yet), whether the mod made it, and the
stage's own `Data`. The reverse index (object -> id) is the registry's, so the two directions cannot drift (the
`g_gv_of[v] == id` checks of BUG-0951 go). No game call inside; liveness is injected.

| API | What | Replaces |
|---|---|---|
| `Registry(name, alive)` | `alive`: `[](const mirror::Ref& r) { return game::alive(game::LiveRef{r.p, r.serial, r.noun}); }`; Civ guest vehicles pass their own rule (below) | `px_alive`, `mirror_ok`, `gv_ok`, `hm_live`, `hv_live`, `kept_alive` on table entries |
| `at(id)`, `find(id)`, `has`, `erase`, `detach(id)` | the entry (made empty: a mirror named before its object exists); `detach` forgets the object, keeps the entry | `g_gm[id]`, `g_proxies[id]`, `px.c = nullptr` |
| `bind(id, ref, made)` | binds; a pointer bound to another id is refused (`refused`) | `g_gid_of[c] = id`, `g_gv_of[v] = id`, `g_proxy_of[c] = id` |
| `adopt(ref, alloc, on_reused, out)` | host: the id of an object of the game's lists; another serial at a known address retires the old id (`on_reused`) and gets a new one | tribe host_tick's serial check (BUG-0951), civ's pol / serial check, npc's id allocation |
| `id_of(p)`, `id_of_live(p)`, `get_live(id)`, `live(entry)`, `for_each_live`, `check_live()` | lookups; a dead object found is detached (`stale`) | `g_hm.find(c)` + `hm_live`, `check_identity` (BUG-0842) |
| `sweep(seen, decide, remove)` | after a complete snapshot (C4) or a host table pass: every unseen entry is `Remove` (the stage destroys a live object), `Keep` (held) or `Defer` | the sweeps of tribe / civ guest_state, npc's stale drop, tribe / civ host_tick's erase of unseen members |
| `defer(ref)`, `pump(hold, destroy)`, `deferred(p)` | the deferred removal path: once a frame, gone objects are dropped, held ones wait, the rest destroyed | civ `g_kill_later` + `hold_removal` (BUG-0493) |
| `ours(p, noun, all_bound)` | the save filter: bound and made by us (Tribe), any bound one (Civ), or deferred | tribe `g_ours_c` / `ours_in`, civ `save_filter` |
| `clear()`, `held()`, `consistent()`, `stats()`, `status()` | lifecycle and coop.status (the counters survive `clear`) | per-stage counters (`g_mirror_stale`, `g_veh_stale`, `g_identity_retired`, `g_removed`) |

Which tables map to it, and what stays in the stage:

| Stage | Table today | Registry | Stays stage-specific |
|---|---|---|---|
| NPC host | `g_host_npcs` (creature -> HostNpc) / `g_host_by_id` (npc.cpp:101) | `Registry<uint16_t, HostNpc>("npc.host")`, `adopt` with `npc::alloc_npc_id` as `alloc`, noun 0x18EB45E | the species / dead reuse rule `npc_object_reused` (BUG-0044, BUG-1100: calls `erase` + `retire` until open question 1 below is answered), `retire_pooled` (a pooled creature: herd null or at the centre, BUG-0274), `g_known` per guest, the diagnostics fields |
| NPC guest | `g_proxies` (id -> Proxy) / `g_proxy_of` (npc.cpp:304, 343) | `Registry<uint16_t, Proxy>("npc.proxies")`, `made` true (spawned); `Proxy::c` / `serial` become the entry's ref | the stale drop's rule (5 s without a state while states flow, BUG-0611's thaw: `Keep`) as the sweep's `seen` / `decide`; herds, the kinematic driving, meat, flock effects |
| Tribe host | `g_hm` / `g_hm_by_id` (tribe.cpp:303) | `Registry<uint16_t, HMember>("tribe.hm")`, `adopt` with `net::alloc_id`, noun 0x18EB4B7 | the owner split (players in turns, `g_born_for`), the bodies out of a list (BUG-0620: `Keep` in the host sweep while `kBodyMax`), the tribe pointer |
| Tribe guest | `g_gm` / `g_gid_of` (tribe.cpp:357, 429) | `Registry<uint16_t, GMember>("tribe.gm")`, `made` true for `spawn_member`, false for a bound local member | the bind candidate (the nearest unbound member within 6 m of the mapped tribe, else spawn), `remove_member`, the orphan sweep of local members the host has not, `g_ours_t` (made NPC tribes), the driving |
| Civ host | `g_hv` / `g_hv_by_id` (civ.cpp:390) | `Registry<uint16_t, HV>("civ.hv")`, `adopt`, noun 0x18C6DE8 | the political id check (BUG-0059: a pol change is an `erase` + new id), owners and orphans (BUG-0031) |
| Civ guest | `g_gv` / `g_gv_of` (civ.cpp:456) and `g_kill_later` (civ.cpp:4013) | `Registry<uint16_t, GV>("civ.gv")` with `alive` = the serial in the vehicle's memory (`object_alive && object_serial == serial`: a vehicle the mod just made joins the game's list later; the one `object_alive` the registry keeps, on a pointer `prune_gone` checked this frame); sweep `decide` = `hold_removal ? Defer : Remove`; `pump(hold_removal, kill_local)` | the bind candidate (same pol / purpose / loco within 25 m, else CreateVehicle; `bound_design`), the orphan sweep of local vehicles (through `defer` when held), `prune_gone`, the velocity estimate |
| Cell | `g_mirrors` / `g_host_of` (cellsync.cpp:143) | none (pool slots with a generation; audit "not worth doing") | all |
| Squadron | `g_mirrors` / `g_mirror_of` (squadron.cpp:97) | optional, not in C1's order | (C4 only) |

Migration steps (order of the plan: NPC, Tribe, Civ; host tables before guest tables inside a stage is not required):
1. Switch `npc.registry` / `tribe.registry` / `civ.registry` (one per stage, BUG-1140).
2. Move the stage's fields that are not the pointer / serial into `Data`; the registry is a file-scope object next to
   the old maps; every `object_alive(` on an entry becomes `live` / `get_live`.
3. Host pass: `adopt` each object of the game's lists, then `sweep(seen-this-pass, decide, retire)`.
4. Guest snapshot: with C4 (below) `rx.finish(reg, decide, remove)`; NPC's stale drop: `sweep` with its time rule.
5. The save filter and the `coop.<stage>` status line read the registry; the LIFECYCLE_STATE / on_world_change clears
   of the old maps become `reg.clear()` in the stage's hook (C2).
6. Lab, on and off: npc: coop_npc_herd_identity, the proxy identity tests of BUG-0842 / BUG-0931, civ_wild_reused,
   civ_wild_pooled; tribe: tribe_mirror_* (lifetime, anims, ground), tribe_chief_removed; civ: civ_mirror_lifetime,
   civ_pause_city, the save strip tests (BUG-0470, BUG-0495); the chaos lane. Then delete the old maps and the switch.

## The snapshot receiver: chunked host snapshots (2026-11-15, feat/w51-ccore)
<a id="arch-snapshot-rx"></a>

C4, BUG-1143. `mods/coop/net/snapshot_rx.h`, `net::SnapshotReceiver<Id>` with a `net::SnapshotPolicy`:
`begin(cycle, chunk, chunks)` (false: drop the chunk: a bad header, more chunks than the policy, a late chunk of an
older cycle), `saw(id)` per entry read, `end(parsed_ok)` (true once, on the chunk that completes the cycle; a cut chunk
does not count), `finish(registry, decide, remove)` (the hand-off: `mirror::Registry::sweep` with "not named by this
cycle"), `reset()` (session / world: BUG-1051; the counters stay), `held()`, `generation` (+1 per cycle started).
Counters: `chunks_in bad older cut incomplete abandoned complete`.

| Stage | Today | Policy |
|---|---|---|
| Tribe | `g_cyc` (net::CycleTracker) + `g_cycle_ids` + `g_incomplete`, tribe.cpp:430-450 and guest_state | default: `{255, g_cycle_order_fix, 0x8000, true, true}` (keep_order follows `net.cycle_order`, BUG-0612, read every chunk) |
| Civ | the same copy, civ.cpp:481-493 and guest_state | the same as Tribe |
| Squadron | `g_rx_cycle`, `g_rx_mask`, `g_rx_done`, `g_partial_cycles`, `g_stale_chunks`, squadron.cpp:100-110 and guest_state | `{31, true, 99, true, false}`: at most 31 chunks, 99 behind is older (farther: a restarted host), any chunk that fills the set completes it (a late chunk 0) |

Migration (switches `tribe.snapshot_rx`, `civ.snapshot_rx`, `fleet.snapshot_rx`): replace `cycle_chunk_ok` +
`g_cyc.begin` + `g_cycle_ids.clear()` by `rx.begin`, each `g_cycle_ids.insert(id)` by `rx.saw(id)`, the tail
(`if (!r.ok()) return; g_cyc.add; if (chunk + 1 < chunks) return; if (!complete) ...`) by `if (!rx.end(r.ok()))
return;`, the sweep loop by `rx.finish(reg, ...)` (with C1) or by the stage's own loop over `!rx.seen(id)` (before
C1). The LIFECYCLE_STATE "tribe.cycle" / "civ.cycle" call `rx.reset()`. One behaviour difference, on purpose: a
duplicate of a completed cycle's last chunk does not sweep a second time (`sweep_once`; the second sweep removed
nothing new). The squadron's `coop.fleet chunkfix 0` path (BUG-0037, the removal on the last chunk without the
completeness check) has no policy: it stays the switch-off branch until B1 retires it. Lab: tribe and civ loss tests
(sync_burst, the cycle-order test of BUG-0612, BUG-1051's rejoin), squadron chunk tests (`coop.fleet drop`).

## The world identity and the lifecycle observers (2026-11-15, feat/w51-ccore)
<a id="arch-world-id"></a>

C2, BUG-1141. Owner decision 2: **an editor visit is the host away (C3), not a world change**; the mirrors stay and an
object the game rebuilt meanwhile is caught per object by C1's liveness. A world change is only a change of the world
id: another stage, another save (planet file), the stage entered again (a load, a received world).

`mods/coop/world_id.h`: `world_id::Tracker::step(Frame{mode, count, save, anchor}, &from, &why)` once a frame;
`world_id::Id{stage, serial, mode, save}`; `Why` = Opened / Closed / Stage / Count / Save / Anchor. The rule and the RE
facts are in the header; in short: mode a stage -> open, new when the stage, its `stage_epoch` counter, the known save
crc (known -> another known) or the non-null root object (Tribe: the player tribe, Civ: the player civilization,
Space: the player cEmpire; Creature and Cell: none) changed; the stage's editors (and kScenario in Space) keep it; any
other mode closes it. The first frame only records. `serial` names a world of this process; across machines a world is
{stage mode, save} (PlayerState `world`, MSG_HOST_WORLD).

The three detectors it replaces: coop.cpp `avatar_change_check` (the local entity's pointer changed, an editor visit
included: npc, nest, fields, wobj and forget_puppets), `tribe::world_check` / `civ::world_check` (a byte copy of
stage_epoch.h Watch plus "left for a non-editor mode"), `lifecycle_world_tick` (the stage counters' sum). The
counters next to them: `game::world_epoch` (Creature, BUG-0119) and `spaceext::world_epoch` (Space, BUG-0225) become
`world_id` serial reads.

`mods/coop/lifecycle.h` observers: `LIFECYCLE_HOOK(name, events, order, fn, needs_game)` with events `kSessionEnd |
kWorldChange | kPlayerJoin | kPlayerLeave` and `fn(const lifecycle::Ctx&)` (`path`, `flags` kCellToo, `player`,
`from_world` / `to_world`, `from_stage` / `to_stage`). `lifecycle::fire(ctx, game_ok)` runs the hooks by `order`
(equal orders: registration order), with the LIFECYCLE_STATE resets of the event's scope at order 0 (hooks < 0 before:
a despawn that reads its table; >= 0 after). `needs_game` hooks run only when `game_ok` (on_frame, outside
`g_sess.update()`, BUG-0029); else they are queued for `lifecycle::pump_game()` (on_frame, phase 1). A `fire` inside a
hook is queued and runs after the fan-out (no nesting); a hook that throws is counted (`faults`, `last_fault`) and the
fan-out goes on; `set_runner` lets the game glue call hooks under SporeKit's fault guard. `hooks_status(event)` for
`coop.lifecycle`. tools/ratchet_lint.py already counts a function named in a LIFECYCLE_HOOK as registered.

Proposed orders (today's call order of end_session block 1 and avatar_change_check):

| Order | kSessionEnd (end_session) | kWorldChange | needs_game |
|---|---|---|---|
| -100 | pfind, support | puppets (forget_puppets; with decision 2 only on a real world change) | yes |
| -90 .. -60 | npc, nest, fields, wobj, realign, parts, effects | npc, nest, fields, wobj | yes |
| -55 .. -25 | social, tribe, cellsync (`flags & kCellToo`), creations, civ, squadron, spacecoop | tribe, civ, Space modules (new: Space joins lifecycle.h) | yes |
| -20 | the remotes despawned, views | | yes |
| 0 | LIFECYCLE_STATE resets (block 3 moves here, CORE-3 step 2) | WORLD_STATE resets | no |
| > 0 | notices, feed | | no |

Migration (the core agent first: switch `core.world_id`; then each module `<module>.world_hooks`):
1. coop.cpp frame_observe: one `world_id::Tracker` stepped right after `game::stage_epoch_tick` with `modes::
   current_world()` (only a real planet file: world_name.h) and the stage's anchor (tribe / civ / spaceext export
   one `void* world_anchor()`); a change -> `lifecycle::fire({kWorldChange, ...}, true)`. `lifecycle_world_tick`
   goes (its resets run at order 0 of that fire). Note: WORLD_STATE now also resets when a world closes (menu, load);
   the resets are idempotent, but `world.host_reload` (BUG-0332) must be checked on the Tribe reload path.
2. end_session: `fire({kSessionEnd, path, cell ? kCellToo : 0}, !inside_update)`; the three hand lists (and the
   "before BUG-0033" ones, B1) go; block 3's globals move into SESSION_STATE entries.
3. The player callbacks: `fire({kPlayerJoin / kPlayerLeave, id})`; `player_life::forget_all` becomes the hook
   "core.player_life" at order 0; the duplicated `g_ready.erase` and the hand call of `npc::on_player_leave` go.
4. Each module registers its `on_world_change` / `on_session_end` as a hook and drops its own detector:
   `tribe::world_check` + `g_world_watch` + `g_world_left`, the same in civ, the `editor_world_fix` /
   `world_epoch_fix` branches of avatar_change_check. Its plain clears go into LIFECYCLE_STATE.
5. Decision 2 in the Creature-stage modules (npc, nest, fields, wobj, puppets): an editor visit no longer calls their
   `on_world_change`; each stored object they keep across the editor is a LiveRef / registry entry instead
   (BUG-0426's rule), so a rebuilt object is caught where it is used.
6. Lab: coop_lifecycle_reset (+ `coop.lifecycle held`), lifecycle_reset_cell, wsync_transitions, the editor round
   trips per stage (tribe_editor_world, the creature editor tests), coop_world_reload (BUG-0119), the leave / rejoin /
   id-reuse tests; for decision 2 a test that the host's Creature editor visit keeps the guests' NPC proxies (FAIL
   with `npc.world_hooks` off: today they are despawned and rebuilt).

## The host-busy model: away, editor, pause (2026-11-15, feat/w51-ccore)
<a id="arch-busy-model"></a>

C3, BUG-1142. Owner decision 1: **the host's explicit pause (P, the pause menu, the Sporepedia, a tutorial box) makes
the guests wait in every stage, with the line "<host> paused the game"; not in Rivals mode; a guest's pause stops
nobody; host away / editor stays its own state.** `mods/coop/busy_model.h`:

* `classify(Reading)` (host): Away (main menu `kWhyMenu`, loading `kWhyLoad`), Editor, Paused (the pause menu reason
  0x4BF38A4, also My Collections; P 0x4BF38A8; the Sporepedia 0x4BF38A6 while no editor is coming; the UI-modal
  0x4BF38A7 with a modal window up; Cell: the pause menu's window), None. The
  game's own stands (a caption's cinematic pause, a stage entry, an editor coming) are None with `kWhyCaption`. The
  reason table: docs/re/host-centric.md#host-centric-pause-reasons.
* `Announcer` (host): the state once on each change (a pause after 1 s, BUG-1086's rule; its end and every other
  state at once), and `current()` for a guest that joins. `encode` / `decode` of the message (below).
* `HostView` (guest): the message of the host (id 0 only: a guest's pause stops nobody; an older seq dropped), else
  the 0.7.14 fallback (`Fallback{host_out_of_stage, tribe kind 21}`); the 0.7.14 waits (menu 1.5 s, editor / load 4 s,
  back 1 s); `step()` returns the `Edge` (NPC destroys its proxies on the edge into Away; the notes are said on edges,
  `note(edge, host, act)`).
* Rows (`rows()`, `row_of(stage)`, `act(stage, edge, rivals)`): Paused -> Wait in every stage (On in Rivals), the
  game's own stand -> Wait in Tribe only (BUG-1086 keeps its caption wait), Editor / Away -> Local in Cell, Creature,
  Tribe, Civ (local_play, npc away locals, cell's own pool), On in Space; commands held in Tribe and Civ, the "waits"
  note in Civ.
* `HeldPump`: `hold(row, from, bytes, world, now)` on the host when its stage cannot run a command, `tick(row,
  active, world, now, run)` (the 1 s active gap and the kSettle of tribe.cpp / civ.cpp tick), `say_due` (Civ's 5 s note
  throttle), `forget(id)` for player_life.

Which code maps to it:

| Stage | Today | With the model |
|---|---|---|
| core | coop.cpp `host_out_of_stage`, `host_away_tick` (the waits and the feed lines), `host_absent_for` | host: classify + Announcer, send MSG_HOST_BUSY; guest: one HostView, `C.host_busy()` for the modules; the away feed lines come from `note` |
| Cell | cellsync.cpp:4530 own pool on `host_away / elsewhere / host_left` | row cell: away / editor Local (elsewhere / host_left stay own_world's); Paused: Wait (new; see the open question on how a Cell guest stands) |
| Creature (npc, nest, realign, worldobj) | npc.cpp's shadow `g_host_away` and its edge, nest.cpp:531, realign.cpp:401, worldobj.cpp:1474 each read `C.host_away` | row creature: the edge into Away destroys the proxies (npc), Paused: Wait (new) |
| Tribe | `local_play` (tribe.cpp:66), the held-command wiring (5829, 5946), kinds 20 / 21 and their waits (2526-2680), the pause net (BUG-1101) | row tribe; the host sends MSG_HOST_BUSY and, for one version, still kind 21 for 0.7.14 guests; a guest that heard MSG_HOST_BUSY ignores kind 21; kind 20 (the game over) stays Tribe's; the pause net counts the shared wait as "held" |
| Civ | `local_play` (civ.cpp:75), held commands (1432, 4818), `say_host_away` | row civ, HeldPump with `say_due` |
| Space | no pause relay | row space: Paused Wait (new), Rivals: On (`spaceext::rivals()`) |

The wire need, **MSG_HOST_BUSY** (for the manifest agent to add; not in the net tables yet):
* Type: the next free top-level type, proposed 70 (after MSG_HOST_WORLD 69). Direction: host -> every guest, reliable,
  Session class (taken wherever the guest plays: away / editor are about the host, not the world). The host-side
  direction gate (B3) drops it from a guest.
* Payload v1, 14 bytes little endian: `u8 version (1) | u8 state (0 none, 1 paused, 2 editor, 3 away) | u16 why
  (kWhy* bits: 1 menu, 2 load, 4 pause menu, 8 P, 16 modal, 32 caption, 64 time stands, 128 Sporepedia) | u32 mode (the host's mode
  id) | u32 world (the host's save crc, 0 unknown) | u16 seq (+1 per message in the host's session)`. A reader takes a
  longer payload (a newer host's tail) and drops a shorter one or an unknown state. Golden bytes in
  tests/core_tests.cpp (busy_codec).
* When: on every change of the classified state (debounced as above) and once to each guest that joins (after
  WELCOME). Nothing periodic: the reliable channel delivers it; a session that dies ends the state with it.
* Compatibility: a 0.7.14 guest gets an unknown type: its session hands it to `cb.custom`, which has no case for it
  (and, when the guest is elsewhere, the world gate drops it as an unknown type): ignored. A 0.7.14 host sends none:
  a 0.7.15 guest stays on the fallback (its PlayerState mode for away / editor / load, Tribe's kind 21 for the pause),
  so it waits for a 0.7.14 host's pause in Tribe only, as 0.7.14 did. A 0.7.15 host keeps sending Tribe's kind 21 /
  20 for one version (0.7.14 guests in Tribe). Family revision (B6): a new member of the Session family; a mismatch
  does not refuse a join (optional message).

Migration (switches: `core.host_busy` for the host's send and the guests' HostView; per stage `cell.busy_model`,
`npc.busy_model`, `tribe.busy_model`, `civ.busy_model`, `space.busy_model`):
1. Core: the Reading from game_busy's readings plus `world::pause_reasons()`, `coop_ui::game_modal_up()` and the pause
   menu window (coop.cpp already looks it up 5 times a second) and `editor_coming` (vote.cpp's editor-pending
   flag, BUG-0920, and a submitted editor request); Announcer on the host, HostView on guests; `C.host_busy`
   in the module context returns the settled Edge; `C.host_away` becomes `act(...) == Act::Local`.
2. The guest's wait (Act::Wait): one pause of ours on the guest's game (cGameTimeManager::Pause with the UI-modal
   reason 0x4BF38A7, as Tribe's wait does today) and the note; resume on the edge back, on a session end, a stage end,
   or the host's Away. Tribe's `gameover_pause` becomes the shared wait plus its kind-20 reasons.
3. Each stage reads its row through `act()` instead of its own `local_play` / `g_host_away` copy; Tribe and Civ use
   `HeldPump` (the PLAYER_STATE entries call `forget`).
4. Lab, a behaviour change: one class test per stage, `coop_host_pause.py --stage cell|creature|tribe|civ|space` (from
   coop_tribe_host_pause.py: the host opens its pause menu 30 s, the guest's game stands within 6 s with the note, the
   guest's order made meanwhile runs after the host resumes, both worlds compared), FAIL with the stage's switch off;
   one Rivals run in Space (the guest does not wait); one run where the guest pauses and the host's game runs on. The
   existing away / editor tests (tribe_away, civ_away, cell away, npc away locals, the held-command tests of BUG-0693)
   pass on and off. CHANGELOG and the announcement get one player line ("When the host pauses, everybody waits").

Open RE questions (for the stage agents):
* Cell: the pause menu sets no cGameTimeManager reason (lane 4, 2026-11-15); how a Cell guest's simulation is stood by
  the mod (SimUpdate does not run under the pause menu, docs/re/cell.md) is not known; P in Cell sets no reason and
  shows no window (not known whether it pauses at all).
* The Sporepedia shares its reason 0x4BF38A6 with "an editor is coming" (lane 4): the host must tell them apart by
  its own editor-pending state (`Reading::editor_coming`); the flags differ (Sporepedia 1), the editor's flags are not
  measured. The Civ planner's reason is not measured (docs/re/civ.md: not known); decision 1 does not list it.
  F1 / F2 open nothing in the probe's games (the docs' lane 7 Tribe guest saw 0x4BF38A4 for them).
* A tutorial box: assumed the UI-modal reason 0x4BF38A7 with a modal window (docs/re/tribe.md#tribe-game-time-pauses);
  not measured with a real tutorial box.
* Whether a pause reason of our own (not 0x4BF38A7) can be used for the guest's wait (the reason map at
  cGameTimeManager +0x4C may need a known key).

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

## The message manifest and the dispatch (2026-11-15, feat/w51-manifest)
<a id="arch-message-manifest"></a>

Every top-level message type is one row of `mods/coop/net/messages.def` (B3 of 0.7.15, BUG-1136): its id, who sends
it, its world-state class, its route in session_glue.cpp's dispatch, which game's dispatch takes it, and its message
family. The order of one message on a game:

1. `net::Session::handle_msg`: the host's direction gate (`net.dir_gate`: a host-only type from a guest is dropped),
   then the session's own types (HELLO, PLAYER_STATE, CHAT, BYE, ANIM's relay, the guest's WELCOME / REJECT / ...);
2. `g_sess.cb.custom` (session_glue.cpp): the world gate (`gate_drops`: a guest elsewhere drops the host's world
   state, host_world.h), then one `switch` over `net::dispatch_route(type, is_host)`, one case per handler module.

A new message type is a row in messages.def and a case in that switch (a new route: a value of `net::Route`, which the
switch must handle: `-Wswitch` with `-Werror`). The Space sub-kinds have their own manifest
(`mods/coop/net/space_kinds.def`, the direction gate of BUG-1041). Details, the docs tables generated from the
manifest and the compatibility rules (message family revisions, B6): docs/04-netcode.md#netcode-manifest and
docs/04-netcode.md#netcode-compat.

## The per-player store on the host's disk (2026-11-15, feat/w51-store)
<a id="arch-player-store"></a>

E1 of 0.7.15 (owner decision 13, BUG-1149). The shared world is the host's and is saved; what belongs to each player
("whose is it") lived only in the host's memory and was lost when the host's game closed. The host now keeps it per
world on its own disk, next to its save:

* **File**: `<Spore>/Games-sporecoop-store/<slot>/<planet>.txt` (`<Spore>` = the profile's AppData `Spore` folder,
  the parent of `Games`), one per world = the host's save slot and planet file (`world::current_game_key`'s last two
  parts, lower case: `game3/madas.spo`). Beside the Games folder, never inside a slot: a world share sends every file
  of the slot to the guests (slotfs::collect) and the game reads the slot folder; the guests never get the file.
  `salt.txt` in the same folder is this host profile's salt for the player keys.
* **Format** (player_store.h, version line `sporecoop-store 1`): `world`, `saved` (unix time), world records
  `w <feature> <ver> <hex>`, players `p <stable key> <seen> <name>` each followed by its records
  `r <feature> <ver> <hex>`, and `end <fnv32>` over everything before it. A damaged or foreign file (another world
  line, a bad sum, another format) is an empty store and is moved to `.txt.bad`. A record of a feature or version a
  build does not know is kept and written back as it was. At most 64 players (the longest unseen go), 256 KB a record.
* **Keys** (the BUG-1107 / BUG-1109 rule): a player is its stable key `s:<16 hex>` = a salted 64-bit digest of
  `net::Session::player_key` (itself a digest of the HELLO rejoin token; the token never reaches the disk); the host
  itself is `host`. The rejoin token is the same game's across restarts (BUG-1135, kept 30 days since BUG-1150), so
  the same player is found again. A player whose token changed (another PC, a reinstall, another host address, more
  than 30 days) is a new player: **its things are not given by name automatically** (another game under the same name
  must not get them). The host is told in its feed ("X joined from a game this world does not know") and may hand
  them over by its word: `coop.store adopt <player> [<stored name>]` moves the records of the one stored player of
  that name (session suffixes " 2".." 9" ignored) who is not in the session to that player, in the store and in
  every consumer's memory (`store::Feature::adopt`).
* **When**: read when the host's world opens (`store::tick`, every frame, the lookup at most once a second); written
  after every save of the game (`store::after_save` from the one hook on `cGamePersistenceManager_SaveGame`,
  space_allies.cpp h_Save; any stage, the player's own save, a share's, an autosave): each consumer puts its records
  (`store::Feature::save`, only the keys and world records it holds in memory for this world: what it never read is
  left as it was), and the file is written outside the frame (`run_outside_frame`): a `.tmp` file, flushed, then
  `MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH)`. A guest keeps no store. A world nobody kept anything for gets no
  file.
* **Consumers** (each behind its switch, each with a lab twin across sessions, docs/06-testing.md#testing-session-restart):

| Feature | Records | Switch | Ticket |
|---|---|---|---|
| Space colonies (space_map.cpp KeptStore through `kept_disk::Backend`) | `space.colonies` v1: u32 PlanetIDs per player | store.space_colonies | BUG-1149 |
| Space badges and rank (space_badges.cpp) | `space.badges` v1: the badge tables (wire/space_badges.h put_badges) | store.space_badges | BUG-1149 |
| Space Rivals (space_rivalry.cpp) | world `space.rivals` v1 {u8 rivals, u16 n, n x (key, key, u8 relation)}; `space.budget` v1 i32 per player | store.space_rivals | BUG-1152 |
| Civ owners of cities and vehicles (civ.cpp) | world `civ.owners` v1 (see civ.cpp) | store.civ_owners | BUG-1153 |
| Tribe owners of members (tribe.cpp) | world `tribe.owners` v1 (see tribe.cpp) | store.tribe_owners | BUG-1154 |

  The Space kept stores (space_kept.h) are keyed by the same stable key (`spaceext::kept_key` = `store::key_of`), so
  `KeptStore::restore()` at world open puts the disk's values where the next join's first report finds them: the
  restore then runs exactly as after a rejoin (K_KEPT / K_BADGES_KEPT / K_SET).
* Status and test seams: `coop.store` (world, players, records, reads, writes, fails), `coop.store save` (as after a
  save of the game), `coop.store reload`.

## The host's autosave (2026-11-15, feat/w51-store)
<a id="arch-autosave"></a>

E2 of 0.7.15 (BUG-0864). While it hosts a session (connected, a world open), the host's game saves itself
(`world::save_game`: the pause menu's Save message into its own slot) every N minutes (the session setting
`all.autosave`, Live, choices off / 5 / 10 / 15 / 30 / 60 min, default 10; coop.ini `autosave` = the choice) and
5 s after a milestone: a later stage of the same world, a colony founded (the host's own or a guest's), a city
captured (Civ), an epic killed (Tribe). Any save of the game counts (the player's own, a world share's): the timer
starts again from it (`store::saves()` / `store::since_save()`). The clock is portable (autosave.h, unit tested in
tests/store_tests.cpp); the glue (autosave.cpp) tries outside the frame and waits while a cinematic plays, a share's
save is being written or the last save's box is up; save_game itself refuses editors, loads, mode switches, the end of
Tribe and Space on a planet (tried again every 5 s); a posted save whose write is not seen in 30 s counts as lost and
is tried again 15 s later. The co-op window's session line (host) shows "saved N min ago". Switch
`coop.fix world.autosave off`: no autosave (0.7.14). Status: `coop.autosave`; test seams `coop.autosave interval <s>`,
`coop.autosave milestone [<why>]`.

## Tribe on the wave-C mechanisms (2026-11-15, feat/w51-tribe)
<a id="arch-tribe-wave-c"></a>

C1, C4, C7 and D3 of docs/plan-0.7.15.md for the Tribe stage (BUG-1140, BUG-1143, BUG-1160, BUG-1161), following
[the recipe](#arch-wave-c):

* **Tables** ([mirror registry](#arch-mirror-registry)): the host's member table is `mirror::Registry<uint16_t,
  HMember> g_hm("tribe.hm")`, the guest's mirrors `mirror::Registry<uint16_t, GMember> g_gm("tribe.gm")`; the hand-kept
  reverse maps `g_hm_by_id` and `g_gid_of` and the `c` / `serial` / `id` fields of the entries are gone (the entry's
  `ref` and `id`). Liveness is injected: the host's `hm_alive` = `kept_alive` (game::LiveRef with the serial;
  `tribe.kept_refs` off: the object's memory), the guest's `gm_alive` (LiveRef; `tribe.mirror_member_lifetime` off:
  the object's memory). Lookups by object are `hm_of(c)` / `gm_of(c)` (a pointer compare), checks of a kept entry
  `hm_ok(e)` / `mirror_ok(e)` (no detach: guest_state alone forgets a dead mirror, with its air hold, BUG-0946).
* **Host pass** (host_tick): every member of the first 32 tribes is `adopt`ed (another serial at a known address
  retires the old id and gets a new one: BUG-0951; `tribe.kept_refs` off: the entry at the address stays); a new
  entry's owner is `tribe_logic::new_owner` (E1's claim from owner_claims.h, else the players in turns; a purchase of
  BUG-0671 over both); the re-split when the players change is `resplit_owner`, a newcomer's share `balance`. Then
  `sweep(seen this pass)`: an unseen entry whose object is a dead member's body is `Keep` (sent with health 0, BUG-0620,
  `tribe_logic::body_hold`: at most kBodyMax 60 s, its last tribe in the table), every other one `Remove` (no game call).
* **Guest snapshot** (guest_state) on `net::SnapshotReceiver<uint16_t> g_snap` ([C4](#arch-snapshot-rx), policy
  `{255, net.cycle_order, 0x8000, sweep_once, last chunk}`; `tribe.cycle` resets it with the session and the world,
  BUG-1051): per entry `g_gm.at(id)`, a dead object detached (`tribe_logic` decides grow up, make a mirror, the bind
  candidate `pick_bind`: the nearest unbound member of the mapped tribe within 6 m, else `spawn_member`, bound with
  `made`); on the completing chunk `g_snap.finish(g_gm, Remove, remove_member when alive)`, then the orphan sweep of
  local members of the mapped tribes that are not bound (stage-specific, as before). The save filter keeps its own
  `g_ours_c` (spawned mirrors by serial): it must outlive the table, which a session end clears while the mirrors stay
  in the guest's world until it leaves it.
* **Member animations** (C7, [netcode](04-netcode.md#netcode-tribe-anim)): `MSG_TRIBE_ANIM` (70, tribe family 1.1)
  with `wire/tribe_anim.h`; `anim_send` picks the type per guest from its HELLO's family list.
* **Rules** (D3): `mods/coop/tribe_logic.h`, tests/tribe_tests.cpp.
* Status: `coop.status` lines `tribe registry:` (both tables' `status()`), `tribe snapshot rx:` and `tribe anim msg:`.
