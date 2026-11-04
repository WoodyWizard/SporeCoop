# 2. SporeKit and the mod toolkit

## 2.1 Mod API (C ABI)

A mod is an ordinary 32-bit DLL that exports a single function:

```c
SPOREKIT_EXPORT const SkModInfo* sporekit_mod_main(const SkHost* host);
```

`SkHost` is SporeKit's function table (versioned, `host->abi_version`):

| Group | Functions |
|---|---|
| log | `log(level, fmt, ...)` → `mods/logs/sporekit.log` + console buffer |
| symbols | `sym(name)` → rebased address or 0; `sym_require(names[])` |
| hooks | `hook_create(target, detour, &orig)`, `hook_enable/disable/remove`: tied to the mod and removed on unload |
| events | `subscribe(event, cb, user)`: preinit / init / post_init / frame / shutdown |
| commands | `command_add(name, help, fn, flags)`: console commands (and, via coop, in the game's cheat console) |
| main thread | `run_on_main(fn, user)`: run on the next frame |
| config | `config_get(section, key, default)` from `mods/<mod>.ini` |
| D3D | `d3d_device()`, `overlay` event for drawing on top of the frame |

`SkModInfo` holds the name, version and `on_unload`. C++ helpers for calling game functions
(`thiscall`/`cdecl`/virtual by index) are in `sdk/include/sporekit_game.hpp`.
Game classes are **not** declared as C++ classes (mingw has a different vtable ABI): fields
are accessed by offset, and calls go through typed function pointers.

## 2.2 Developer console (devkit)

SporeKit runs a TCP server on `127.0.0.1:47800` (+N for instance N, see 6).
Protocol: a command line → a single JSON line `{"ok":true,"out":"..."}`.
By default commands run on the game's main thread (between frames);
commands with the `ANY_THREAD` flag (`ping`, `log`, `threads`, `stack`) work even when
the game is frozen, which shows exactly where it hangs.

Built-in commands:

| Command | What it does |
|---|---|
| `help`, `info` | command list; version, exe hash, base, FPS, frame number, mods |
| `log [n]` | last log lines |
| `sym <name|address>`, `sym.find <substring>` | symbol database lookup |
| `mem.read <address> <len>`, `mem.write <address> <hex>` | process memory |
| `mem.ptr <address> <off>...` | dereference a pointer chain |
| `call <address|symbol> <cdecl|stdcall|thiscall> [args]` | call a game function (RE experiments) |
| `cheat <string>` | run a cheat via `App::cCheatManager` (stage transitions, DNA…) |
| `shot [path]` | back-buffer screenshot to PNG (Linux path, `out/shots/` by default) |
| `key <vk> [tap|down|up]`, `text "..."`, `click x y`, `move x y` | input into the game window |
| `mods`, `mod.reload <name>`, `mod.unload`, `mod.load` | mod management |
| `game.mode` | active game mode (menu, creature, editor…) |
| `stack` | main thread stack (for hangs; the stack is copied while the thread is suspended, named after the resume) |
| `hangdump` | main thread stack with DLL names to the log (works even under the loader lock) |
| `exc [clear]` | exceptions caught by the logger (address, nearest symbol, registers) |
| `bg on\|off` | background mode: the game renders without window focus |
| `wait.frames <n>` | wait for n frames |
| `quit` | close the game |
| `kit.fix [name [0\|1]]` | fix switches of SporeKit (`stackdump`, `cheatwait`, `reentry`, `hookkeep`); 0 = the old behaviour, for tests |
| `kit.test.stackheap`, `kit.test.logfault`, `kit.test.excfault`, `kit.test.hookchurn` | force the conditions of BUG-0050 / 0115 / 0132 (tools/tests/kit_freeze.py) |

Mods add their own commands (`coop.*`).

### Bridge to the Spore cheat console

SporeKit registers a `sk <devkit command>` cheat in the game itself (the `Ctrl+Shift+C` console)
(e.g. `sk info`; commands flagged `SK_CMD_NOT_ON_MAIN`, which wait for frames or stop the main
thread: `shot`, `wait.frames`, `stack`, `hangdump`, are refused there, BUG-0065), and mods register their own keywords via `host->cheat_register`:
`coop host` becomes the devkit command `coop.host`, and the output is printed to the game console.
The cheat is implemented as a fake `ArgScript::ICommand` with a hand-built MSVC vtable.

### Diagnostics

* **Exception logger** (VEH): access violations and the like are logged once per address with
  the nearest symbol from the database. Wine often swallows exceptions in window callbacks,
  and without this the game just "freezes". It is not re-entered by a fault inside its own lock or
  the log's lock (per-thread flags: the fault is recorded, its log lines are dropped; BUG-0115), and
  it names the module (loader lock) and logs only after it let go of its lock.
* **Watchdog thread**: if the main thread makes no progress for 15–30 s or the loader lock
  is held for >10 s, the culprit's stack is written to the log. While the thread is suspended only
  its registers and stack are copied (no allocation, no lock: a thread suspended inside `malloc`
  holds the heap); names and text come after the resume (BUG-0050). No frames while the game loop runs
  (`App::Canvas::Tick` returns, counted by `watchdog_loop_tick`) is a pause, not a hang: the game window
  minimized (Canvas::Tick sleeps 200 ms per pass), unfocused without background mode, or a lost device after an
  alt-tab out of full screen. Then one INFO line "watchdog: no frames for N s, the game loop runs (window minimized
  1, ...)" and no dump, no hang report; a stall is counted from the later of the last frame and the last loop pass,
  so a real hang (no frames, no loop) still dumps after 30 s (BUG-0347, `kit.fix hangloop`; test commands
  `kit.test.window minimize|restore|hide|show`, `kit.test.hang [ms]`, test kit_watchdog).
* **Hook removal** (unload, hot reload): the trampolines of removed hooks are kept (a game worker
  thread may still be inside a detour and call the original; RandomInt runs on worker threads), and
  a mod with hooks gets 50 ms before `FreeLibrary` (BUG-0132). Not a guarantee for a detour that
  blocks for longer.
* **Main-thread fallback channel**: console tasks run not only in `Present` but also
  via a window message (`WM_APP+0x5C0`), so commands work on loading screens and
  when no frames are being drawn.
* **Second frame source** (0.3.3): another overlay can take `Present` over and call the driver
  past SporeKit (the Steam overlay on Windows, issue #4). When `Present` has not been seen for 2 s
  while the window has the focus (or background mode is on) and is not minimized, the frame
  (`SK_EV_FRAME`, the main-thread tasks) runs after every `App::Canvas::Tick` of the game loop
  instead; `SK_EV_OVERLAY` does not fire then. `[sporekit] frames=loop` / `frames.source loop`
  ignores `Present` for tests; `info` shows the source.

## 2.3 `sporectl` (Linux)

`tools/sporectl.py` is the console client. Examples:

```bash
sporectl info
sporectl sym.find GetAvatar
sporectl mem.read 'Simulator::cGameNounManager::Get' 32
sporectl cheat "addDNA 1000"
sporectl shot                    # → out/shots/shot-<time>.png
sporectl wait-mode Creature 120  # wait for a mode switch (polls game.mode)
sporectl -i 2 coop.status        # instance #2 (port 47801)
```

It is also used by the automated tests (`tools/tests/*.py`).

## 2.4 Hot reload

`make coop && sporectl mod.reload coop`:
1. SporeKit calls the mod's `on_unload`, removes all its hooks and subscriptions, and deletes its commands.
2. `FreeLibrary`, copies the fresh `coop.dll` to `mods/.shadow/coop-<n>.dll` and loads it
   (the original stays unlocked, so the build can overwrite it).
3. Calls `sporekit_mod_main` → the mod reinitializes right in the current game mode.

The mod itself is responsible for deleting the game objects it created on unload
(for coop, the spawned "puppets").

## 2.5 Screenshots and overlay

The `Present` hook gives access to the D3D9 device: `shot` copies the back buffer
(`GetRenderTargetData`) and writes a PNG. The overlay (text on top of the game: session status,
nicknames above creatures, chat) is drawn in the same hook before calling the original `Present`.

## 2.6 Multi-instance mode

The command-line switch `-sporekit-instance:N` (N≥2; Bottles filters environment variables):
* console port `47800+N-1`, default coop port `47810+N-1`;
* profile `instN`: a `SHGetFolderPathW` hook → Spore data in `AppData/Roaming/SporeKit-instN`
  (separate saves and caches, no lock conflicts); `-sporekit-profile:<name>` does the same
  for any name (test profile `test`);
* bypassing the single-instance check: renaming `CreateMutexA` mutexes
  (`Global\SporeEdi` and others).

This way a host and a guest run on one machine for a full test.

## 2.7 Diagnostics (SporeKit 0.3.4, `sporekit/diag.cpp`)

For bug reports of players, who send a file, not a debugger session:

* **Logs are kept.** `log_init` moves the previous `sporekit[.N].log` to
  `mods\logs\previous\<name>-<time of its last line>.log` (the newest 10 of each name).
* **The environment is in the log.** At boot: Windows (or `Wine x on Linux`, with the emulated
  Windows version), RAM, the game's 32-bit address space in use and the exe's large-address flag
  (a 4GB patch), `game build: GOG 2024 / Steam (the GOG 2024 code) / GOG 2016 / unknown`; after 30
  frames the GPU (adapter, driver, resolution) and other DLLs that hook the game (Steam, Discord,
  NVIDIA, EA overlays, RivaTuner, ReShade, dgVoodoo, OBS, Spore ModAPI, a d3d9/dxgi/dinput8 from the
  game folder). Console: `env`.
* **Crash reports.** SporeKit's unhandled-exception filter stays on top (a later
  `SetUnhandledExceptionFilter`, also the NULL of `__report_gsfailure`, becomes the next filter in its
  chain) and writes `mods\logs\crash-<time>.txt` + a minidump `.dmp`: the exception (code, address
  with module+offset or the exe's symbol, read/write address), registers, the call chain (frame
  pointers, then return addresses on the stack, with names), every mod's `diag_context`, the
  first-chance faults, every module, the last 300 log lines. The game's own handler catches faults
  of its main loop (a fault in the frame never reaches the filter, the game stops drawing instead),
  so two more paths: `ExitProcess` / `TerminateProcess` of the game within 15 s after a fault (or
  with an error code) write the crash report of the last fault, and the watchdog writes a `report`
  "hang" once when there are no frames for 30 s while the game window has the focus.
* **Reports on request: one archive.** Console `report [reason]`, the mods' `H->diag_report` (the
  co-op window's "Report a problem" button) write `mods\logs\SporeCoop-report-<time>.zip`
  (`sporekit/zip.cpp`, stored entries): README.txt, a fresh `report-<time>.txt` (the crash report's
  sections without an exception), `logs/` (every instance's current log), `crashes/` (the newest 3
  crash reports with their minidumps), `reports/` (the newest 3 earlier reports: hangs),
  `previous/` (the newest 6 kept logs), `settings/` (mods\*.ini, `last_address` hidden). Logs are
  cut to their last 8 MB, the archive stays under 24 MB (GitHub takes 25 MB): the older optional
  files go first and `LEFT_OUT.txt` names them.
* **The archive by itself:** a crash (after its report, in the same thread, inside the filter's
  15 s), a hang report and the end of the game after a fault write the archive too (the problem's
  report as its main report) and `logs\last-problem.txt` ("<what>\n<zip>"). The next boot reads and
  removes it; `H->last_problem()` returns it once per run and SporeCoop tells the player in the menu
  ("the last game crashed. A report to send is ready: mods\logs\SporeCoop-report-....zip").
* **Faults in our code.** A first-chance fault inside `sporekit.dll` or a DLL under `mods\` fires
  `SK_EV_FAULT` (on the main thread at the next frame, arg "address module+offset"): SporeCoop tells
  the player once per run where to find the report.
* **API (since 0.3.4, check `size`):** `kit_version`, `build_name`, `diag_report(reason, out, cap)`,
  `diag_context(mod, fn, user)` (a text block of the mod's state, built by the mod on the frame: a
  crash report must not read game objects), `logs_dir`.
* Tests: `tools/tests/diag_check.py [--crash]` (suite `diag`); `crash.test thread|main` makes the game
  crash (thread: an unhandled fault; main: one the game's handler swallows).


## Crash reports in hard cases, old reports, Unicode settings, parked hooks (2026-10-28, fix/w3-sys)
<a id="modkit-crash-hard-cases"></a>

* **Stack overflow (BUG-0056).** The faulting thread has about one page of stack left: the
  exception logger only copies the record and context into statics (no lock, allocation or log),
  and the unhandled filter only signals a thread made at boot (`so_report_thread`) and waits for
  it: that thread writes the short report, the log line, the full report, the archive and the
  minidump on its own stack. The game's main thread asks for 64 KB of stack guarantee at boot
  (`SetThreadStackGuarantee`; Windows). Under Wine a worker thread's overflow kills only that thread
  and the game goes on; the report is written all the same. `crash.test stackoverflow`,
  `kit.fix stackov 0` = the old path (no report under Wine).
* **Out of memory (BUG-0127).** Every step of a crash, hang or exit report (`report_step`) catches
  its own failure (`std::bad_alloc` left the report thread before: `std::terminate`, no minidump,
  no `last-problem.txt`). The mark names the crash `.txt` first, then the archive once it exists;
  the minidump is written even when the archive failed. `crash.test oom` makes every archive fail;
  `kit.fix crashsafe 0` = the old order without catches.
* **Old reports (BUG-0137).** After every archive and at boot, `mods\logs` keeps the newest 5
  `crash-*.txt`, 5 `crash-*.dmp`, 5 `report-*.txt` and 3 `SporeCoop-report-*.zip` (names carry the
  time); files that `last-problem.txt` (or the previous run's mark) names stay. `kit.fix logprune 0`.
* **Settings in Unicode (BUG-0142, `sporekit/inifile.cpp`).** The profile API reads and writes a
  file without a BOM in the ANSI codepage ("Вася" was saved as "????", a UTF-8 file edited by hand
  read as "JÃ¶rg"). A `mods\*.ini` with text that is not ASCII, or getting such a value, is
  rewritten once as UTF-16LE with a BOM (from UTF-8 when it is valid UTF-8, else from the ANSI
  codepage; a temp file and a rename). ASCII-only files stay as they are (tools edit them with sed;
  `tools/spore.sh` edits only sporekit.ini). Report archives convert UTF-16 settings to UTF-8.
  `kit.fix iniutf 0`.
  Since BUG-0245: a UTF-16 BE file is converted losslessly, a file with NUL bytes (UTF-16 without
  a BOM) is left as it is, a named mutex per file (`SporeKitIni-<hash>`, 500 ms) guards the
  conversion and every write against a second instance, and a non-ASCII value is not written when
  the file could not be made Unicode (`kit.fix iniencode 0`, `kit.fix inilock 0`).
* **Crash reports name the faulting thread (BUG-0248).** The exception line of a report written by
  the report thread or at exit names the thread that faulted (`kit.fix crashtid 0` = the writer).
* **The exit waits for an in-game update's file swaps (BUG-0243).** ExitProcess and a
  self-TerminateProcess wait up to 5 s for the update lock of the game folder (`kit.fix exitwait 0`).
* **Parked hooks (BUG-0202).** MinHook writes a 5-byte jmp over the target without stopping other
  threads (its freeze deadlocked under Wine): a thread in the first bytes continued inside the jmp.
  Every hook now jumps to a SporeKit stub `jmp [cur]`; making (after the first time), enabling,
  disabling and removing a hook only switch `cur` (an aligned dword). A removed hook is parked
  (`cur` = the trampoline, the original code) and the same target hooked again (a hot reload)
  takes it over: the game's code is written once per process. The first hook of a target is still
  a plain write (at boot, before the game's threads call it). `kit.fix hookpark 0` = MinHook's
  create / remove each time.
  CORRECTION (2026-11-01, fix/w33-null-call): not every first hook is made at boot: the devkit's
  input commands hook GetAsyncKeyState / GetKeyState at the first injected input, coop hooks Cell
  SimUpdate at its first cell (see "A new hook's original before its jump" below, BUG-0398, and
  BUG-0403 for the jmp write itself).

## Support diagnostics in SporeKit 0.3.5 (2026-10-29, feat/w7-diag)
<a id="modkit-support-035"></a>

* **API (since 0.3.5, check `size`):** `log_session_clock(offset, on)`: every log line carries
  `[s <now + offset>]` after the local time (a co-op session's common clock, docs/REPORTING.md#reporting-support);
  `diag_headline(mod, text)`: a block at the top of reports (`== Summary`), copied, removed when the
  mod unloads; `diag_files()`: the game files part of the environment.
* **Masked paths.** Log lines and report texts have the game folder as `<game>`, `%USERPROFILE%`
  and Wine's `<home>` instead of the player's paths (`mask_paths`, log.cpp: case-insensitive, `/`
  and `\` alike, set once at `log_init`).
* **Game files** (`diag_files.cpp`, computed once, logged as `files:` after the GPU line, in the
  report and in `env`): other DLLs in `mods\`, DLLs next to the exe that the game does not ship,
  a ModAPI folder, extra `.package` files in `Data\`, `DataEP1\` and the player's
  `AppData\Roaming\Spore` (name, size, date; at most 12 per list) and the sizes of the GOG 2024
  build's 12 base packages and exe.

## A new hook's original before its jump (2026-11-01, fix/w33-null-call)
<a id="modkit-hook-original"></a>

* A detour calls the original through the pointer `hook_create` fills (`o_X`). For a target hooked
  for the first time the stub path set that pointer only after MH_EnableHook had made the jmp live:
  a thread calling the target in between ran the detour with `o_X` null and called address 0
  (BUG-0398: test guests' main thread in GetAsyncKeyState while the console thread made the input
  hooks; one froze). Since `kit.fix hookorig` (on): `*original` is set first, then the stub's `cur`
  = the detour, then the jmp is written; the take-over of a parked stub also sets `*original` first.
* How it looks in a log: `exception 0xC0000005 at 0x00000000`, the record's first stack word a
  return address into sporekit.dll or a mod (the detour), the next one the detour's argument; the
  "calls" line scans only the exe, so it shows the game's caller of the hooked function or stale
  frames, never the detour.
* Still open (BUG-0403): the first write of the jmp itself can catch a thread inside the first 5
  bytes of a function hooked after boot (BUG-0202's crash, nanoseconds per call).

## Report screenshots (SporeKit 0.3.6, 2026-11-01, feat/w35-report-shot)
<a id="modkit-report-shots"></a>

`SkHost::report_shot(tag, caption, out, cap)` / `report_shot_pending()` (`sporekit/reportshot.cpp`,
`sporekit/jpeg.cpp`), used by SporeCoop for every MARK and "Report a problem" (BUG-0442,
docs/REPORTING.md#reporting-shots):

* **The request never waits.** It only records the wish (main thread) and returns the file name
  (`shots/<tag>-<date>-<time>[-s<session second>].jpg`). Refused (a log line, return 0) when a shot
  is still being written, another was requested under 2 s ago, or there is no device yet. A request
  no Present picks up within 3 s (the game does not draw, or frames come from the game loop because
  another overlay took Present: 2.2 "Second frame source") fails with a log line.
* **The capture** runs in the Present hook after the overlay pass (the frame the player is about to
  see, overlay included), on the device's own thread, never from a console thread (the BUG-0065
  rule: nothing waits for the main thread): `TestCooperativeLevel` (a lost fullscreen device: no
  shot), the back buffer (a multisampled one resolved first) `StretchRect`-ed with the linear filter
  into a 1280-wide X8R8G8B8 render target (any back buffer format converts), one
  `GetRenderTargetData` of that small surface, the rows copied out. Measured under DXVK (lab, 1440x1080
  window): 10-19 ms on that one frame, no `perf: hitch`. If the GPU cannot scale it, a plain 32-bit
  back buffer is read in full and shrunk on the worker (box filter).
* **The JPEG** (baseline, 4:2:0, the standard tables, quality 80; again at 65 / 50 / 35 while over
  450 KB) is made by a worker thread (30-50 ms for 1280x960) and written as `.tmp`, then renamed;
  `<name>.jpg.txt` beside it holds "<size> | <caption>". `mods\logs\shots` (`shots-i<N>` for
  instance N > 1, so the lab's two games of a lane keep theirs apart) keeps the newest 5 (by time);
  boot removes unfinished `.tmp` files. `report_shot(NULL, NULL, ...)` removes them all (the
  player's `[report] screenshot=0`).
* **Reports:** every archive adds the kept JPEGs under `shots/` (optional entries: dropped first when
  the archive would pass 24 MB); the report text gets `== Screenshots` before the log lines. A
  crash, hang or exit report (written by `report_step`, outside the frame) takes no screenshot of its
  own: D3D from a crash handler or another thread while the game is stopped is not safe, and the
  text says so.
* **Native D3D9 on Windows:** the same calls (no DXVK-only path); fullscreen and windowed differ only
  in the device-lost case above. Tested in the lab under Wine/DXVK (windowed); Windows and an
  exclusive fullscreen device are not tested yet.
* Tests: `tests/jpeg_tests.cpp` + `tools/tests/jpeg_check.py` (`make test`: the encoder decoded by
  Pillow against its source, the shrink), `tools/tests/coop_report_shot.py` (lab `report_shot`).

