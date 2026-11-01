# 5. Reverse engineering

## 5.1 Target and version

`SporebinEP1/SporeApp.exe` (GOG, 2016): MSVC 2008, x86, no RTTI (2 type descriptors in the whole
exe), `DYNAMIC_BASE`, has `.reloc`. Imports only `SteamApps`, `SteamAPI_Init`,
`SteamAPI_Shutdown` from `steam_api.dll`. Size 24 888 320 bytes - does **not** match any
build known to Spore ModAPI (Disk: 24 904 192 / 24 909 584; March2017: 24 885 248 /
24 895 536 / 25 066 744).

### 5.1.1 The GOG 2024 build (dc04aee5)

**The only supported build from 0.8 on** (decided 2026-10-23, CLAUDE.md). `overrides_922af7c7.sym`
and `gog2016_922af7c7.sym` are frozen: `tools/re_lint.py` only notes symbols missing there.

The current GOG download (an update with PE timestamp 2024-09-30) ships another
`SporeApp.exe`: 24 895 536 bytes, SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`,
image base 0x400000. Its code layout is ModAPI's **March2017**: 2156 of the 2160 high-confidence
`m17=` addresses of `gog2016_922af7c7.sym` are function starts in it. So:

* `data/symbols/march2017_dc04aee5.sym` is the m17 column of the GOG 2016 file, used as it is.
* `data/symbols/overrides_dc04aee5.sym` has every symbol the code uses, ported from
  `overrides_922af7c7.sym` on 2026-10-23 and each checked in the disassembly (prologue, ret N,
  offsets, constants, callers/callees, vtable slots). Then all five stages were played as host +
  guest on Windows 11 (two instances on one PC): joining, world transfer, puppets, NPC damage via
  the host, shared DNA/money/food, civ buy/build/move, tribe orders, ship damage, saving.
* Deltas GOG 2016 → GOG 2024 are piecewise constant but change often inside one area, so a
  delta taken from the neighbours was wrong for about 1 in 10 symbols (it landed mid-function or
  on the function before). Examples: GetAllUFOs +0x80 between neighbours at +0x30/+0x40;
  cVehicle::Die +0x20 next to SetIsSelected +0x10; BT_HealTick -0x60 next to BT_ValidTarget -0x70.
* `.data` globals: -0xFE0 around 0x167xxxx..0x16Dxxxx (cell, tribe UI, space, simulator managers),
  -0xFF8 for the App block at 0x15FExxx (ResourceManager_ptr, FrameLimit_ptr), -0x1000 for the
  social session pool at 0x1594xxx and the cell static ref table at 0x15A8xxx.
* `.rdata` vtables: -0xB18 (citizen, species goal), -0xB30 (terrain texture job), -0xB48 (vehicle,
  cTreeData), -0x958 (SporeStdDrawable). Find a vtable through the constructor that writes it.
* Some code did not move at all: the city planner (its AddBuilding return address 0xD1419C is the
  same), TimeOfDay, cell E68xxx..E7Fxxx.
* Players of different builds cannot play together: the handshake compares the first 4 bytes of
  the hash of the build whose symbols are loaded (REJ_BUILD).
* `code_sha256` (`tools/exe_info.py`, same code in sporekit/sha256.cpp and installer/setup.cpp):
  SHA-256 of ImageBase, the entry point and every section but `.rsrc` (name, address, size, raw
  bytes). Symbol files carry it next to `exe_sha256`; an exe without its own files uses the files
  whose `code_sha256` matches (a re-signed exe, other resources, another header timestamp).
  GOG 2024: `5a8267b2…`. GOG 2016's is unknown (no copy of that exe here). A Steam exe with
  SteamStub DRM keeps `.text` encrypted on disk, so it would not match even with the same code.

### 5.1.2 Windows relocates the exe

`SporeApp.exe` is `DYNAMIC_BASE`. Windows 11 loads it at 0x410000 (Wine keeps 0x400000) and
rewrites `OptionalHeader.ImageBase` in the mapped header to the actual base. SporeKit used to read
the preferred base from that header, so `rebase()` did nothing and every symbol was 0x10000 off;
it now reads ImageBase from the exe file (`symbols.cpp file_image_base`). The log line
`base=0x00410000 (preferred 0x00400000)` shows it is right.

### 5.1.2 The Steam build (3010a413): GOG 2024 under SteamStub (2026-10-24)

Steam's `SporebinEP1\SporeApp.exe` since the October 2024 update: 25 066 744 bytes, SHA-256
`3010a413f9a4a05ee9c4de24756c1c94c7fb218f70cad29bcf5bc0b4abfa2466` (ModAPI's DllModAPI.cpp lists
this size as a "steam patched" March2017 exe). Section by section against the GOG 2024 exe: `.rdata`,
`.data`, `.tls`, `CONST`, `.rsrc`, `.reloc` are byte for byte the same, `.text` (same size) differs on
every page (encrypted), and an 8th section `.bind` (0x29CD0 bytes, SteamStub) holds the entry point
(RVA 0x190E310; GOG's entry point is RVA 0xDE11E1). After the unpacker ran, the `.text` in memory
(relocations undone) has exactly the SHA-256 of the GOG 2024 file's `.text` (✔ runtime, Proton,
2026-10-24).
* SporeKit (0.3.2): the symbol header of `overrides_dc04aee5.sym` carries `data_sha256` (every section
  but .text/.bind/.rsrc, `tools/exe_info.py`) and `text_sha256` (the GOG 2024 .text). An exe without
  files of its own and without the same `code_sha256` whose `data_sha256` matches is "pending": the
  boot (run by the loader at the exe's entry point, i.e. inside the unpacker) does everything that
  does not need game addresses and hooks kernel32 `GetSystemTimeAsFileTime` / `GetStartupInfoW`. Their
  first call from `.text` is the game's own CRT start (`__security_init_cookie` at 0x11E1588 calls
  GetSystemTimeAsFileTime from 0x11E15C5), after the unpacking and before WinMain: there the in-memory
  `.text` hash is checked, the symbols are loaded, then the lifecycle hooks and the mods. A different
  hash leaves co-op off as before. `[sporekit] defer_symbols=1` runs the same late boot on any exe
  (tests of the path on GOG lanes).
* Testing: the Steam game runs only under Steam (the unpacker needs the Steam client; Steam may stay
  in offline mode); launch it with `steam steam://rungameid/24720` and move its window
  (`steam_app_24720`) with `agent-ws assign`. A Spore window that never had the focus does not draw
  (frame 2 forever): for tests set `background_mode=1` in the Steam install's `mods\sporekit.ini`
  (remove it afterwards). Its console is instance 1 (127.0.0.1:47800): use GOG lanes >= 2 for the
  other players.

## 5.2 Method

1. **Porting ModAPI addresses (March2017 → GOG 2016)** - `tools/addrport/port_modapi.py`.
   A "does this address look like a function start" check showed that code in the two builds
   matches in chunks, with piecewise-constant offsets (0, ±0x10…0x90). The script finds function
   starts in our exe (`call rel32` targets + code pointers in `.rdata/.data`), then a Viterbi pass
   aligns the sorted list of ModAPI functions against the candidate offsets.
   Result: 1973 symbols (1744 high, 196 medium, 33 low) → `data/symbols/gog2016_922af7c7.sym`.
2. **Verification with REA** - Ghidra takes longer than the REA limit (330 s) to analyze the full exe, so
   `tools/addrport/pe_window.py` makes a "windowed" copy of the exe: same headers, imports,
   `.rdata/.data`, but `.text` is cut down to the needed window (e.g. 0xC00000–0xC30000). Addresses
   match the real ones, and analysis takes seconds. Verified addresses → `overrides_922af7c7.sym`.
2b. **Private full Ghidra projects and batch queries** (`tools/ghidra/`, 2026-10-23). One project
   per build in the main checkout's `out/` (not in git, ~330 MB each): `out/ghidra-full/` (GOG 2016)
   and `out/ghidra-2024/` (GOG 2024). They are our own, not the shared REA session other agents use.
   * Build: `tools/ghidra/mkproject.sh --build 2016|2024 [--exe <SporeApp.exe>]` (defaults:
     `_ref/gog2016/SporeApp.exe` next to the SPORE folder, and `SporebinEP1/SporeApp.exe` of
     the main game; the exe is only read and must have the build's sha256). Ghidra 12.1.4
     (`~/.local/opt/ghidra_12.1.4_PUBLIC`, JDK 21 in `~/.local/opt`) auto-analysis takes 8-11 min,
     both builds can run in parallel. Then `SporeSetup.java` (1-2 min): Ghidra's analysis misses many
     functions (619 of the 2598 named GOG 2016 addresses had none), so it adds functions at
     `call rel32` targets found in bytes Ghidra did not disassemble (~530) and at code pointers in
     `.rdata`/`.data` (vtables, callback tables, ~32 000), each only at a plausible MSVC start
     (after CC/C3/90 padding or `ret N`) outside existing functions, and re-runs analysis: 58 700 →
     91 700 functions. Then it applies every `data/symbols/*_<sha8>.sym` of the checkout as names
     in their C++ namespaces (base file first, overrides win the primary name, other names at the
     same address stay as labels, `; low` entries are skipped) with the `.sym` notes as plate
     comments. Check after the build: every named function address but the `_Ret` return
     addresses is a function entry. The new project replaces the old one at the end, so queries
     keep working meanwhile. After editing `.sym` files: `mkproject.sh --build … --names` (~5 s).
   * Query: `tools/ghidra/q.sh [--build 2016|2024] <queries-file|-> <output-file>` (default 2016,
     works from any worktree). One batch takes 3-5 s (JVM and project open; a decompile ~0.5 s,
     an `info`/`xref` ~1 ms; the first run after a change of Q.java compiles it, +4 s), so put many
     queries in one file. Example:

     ```
     printf 'dec Simulator::cGameDataUFO::TakeDamage\ncallers 0x00C3CEA0\n' > q.txt
     tools/ghidra/q.sh q.txt out.txt                 # GOG 2016
     tools/ghidra/q.sh --build 2024 q.txt out24.txt  # same names, GOG 2024 addresses
     ```

     One query per line (`#` comments). `<a>` is a hex address (`0x00C3CEA0`, `C3CEA0`), a
     `.sym` name, or either plus an offset (`Name+0x1C`):
     `dec <a>` decompile the function containing `<a>` (creates one if none, not saved),
     `asm <a> [n]` n instructions (default 40, disassembles on the fly), `fasm <a>` the whole
     function, `info <a>` function/symbol/block/notes, `xref <a>` references to `<a>`,
     `callers <a>` / `callees <a>` by function with the call sites, `calls <a>` raw E8/E9 rel32
     sites aimed at `<a>` (also in bytes Ghidra did not disassemble), `scan <hex-dword>` every
     occurrence of a 32-bit value (vtable / global pointers, BT node tables in .data),
     `bytes <pattern>` with `??` wildcards, `str <text>` ASCII and UTF-16LE strings with their
     references (or raw pointers to them), `dd <a> [n]` dwords with what they point to (vtables),
     `sym <substring>` names. Every answer starts with `=== <query>` and ends with its time.
   * Concurrency: Ghidra locks a project even with `-readOnly` (a second run fails with
     `LockException: Unable to lock project`), so each q.sh run queries its own reflink snapshot in
     `out/ghidra-runs/` (0.05 s on btrfs, removed afterwards) under a shared `flock` that
     mkproject.sh takes exclusively while it swaps or renames. Four parallel runs (3× 2016 +
     2024, right after a script change that forced a recompile) took 8-9 s each, same output.
   * Known wrong ported names found this way (GOG 2024 `march2017_dc04aee5.sym`, mid-function):
     `UTFWin::CascadeEffect::GetInterval=0x0097EE80` (inside `ret 4` of 0x97EE70),
     `UTFWin::SporeTooltipWinProc::GetProxyID=0x008365C0` (inside 0x8365A0),
     `Simulator::TimeAtStartOfFrame=0x00B63980` (inside 0xB63650).
3. **Runtime verification** via devkit: `mem.read`, `call`, the exception logger (`exc`),
   the watchdog thread (`hangdump`).

## 5.3 Confirmed functions

| Symbol | Address | How confirmed |
|---|---|---|
| `cCreatureAnimal::Create` | 0x00C09B00 | REA asm: cdecl `(const Vector3* pos, cSpeciesProfile*, int age, cHerd*, bool isAvatar, bool)`; age → +0xB34, herd → +0x1674, calls `SetAvatar` when isAvatar |
| `cCreatureAnimal::Update` | 0x00C0ADF0 | REA: virtual 0x60; if `!(+0xB58 & 0x200)` → `NPCTickAI`, otherwise `AvatarTickAI` |
| `cCreatureAnimal::NPCTickAI` | 0x00C09530 | REA callers: the only call is from Update |
| `cCreatureAnimal::GetHerd` | 0x00C046B0 | runtime exception: returns mHerd, callers don't check for null |
| `cGameNounManager::Get` | 0x00B3D3D0 | REA (call from Create) + runtime |
| `cGameNounManager::CreateInstance` | 0x00B20B80 | REA: Create calls it with noun 0x18EB45E |
| `cGameNounManager::DestroyInstance` | 0x00B224F0 | REA: error branch of Create |
| `cGameNounManager::SetAvatar` | 0x00B1FC70 | REA |
| `cCreatureAnimal::TakeDamage` | 0x00C075A0 | runtime: `cCombatant` vtable[6] of cCreatureAnimal (`this` = creature+0x5A8), `ret 0x14`: `(float dmg, u32 attackerPolitical, int type, const Vector3*, cCombatant* attacker)` |
| `cGameNounManager::CreateHerd` | 0x00B238B0 | REA: null only if species == null; `herdSize=0` → empty herd; herd fields: position +0x34, species +0xA4 |
| `Editors::cSpeciesManager::Get/GetSpeciesProfile` | 0x00401090 / 0x004DF580 | runtime: avatar species key → its profile |
| `cCreatureBase::PlayAnimation(To)` | 0x00C12430 / 0x00C14AE0 | REA + `ret 0xC` / `ret 0x10` |
| `ArgScript::PrintF` | 0x00841050 | REA: vsnprintf into a 0x400 buffer + write to stream |
| `App::cAppSystem::PreInit/Init`, `cSporeApp::Init/Shutdown` | 0x7E8BD0 / 0x7E67E0 / 0xF47E90 / 0xF475A0 | runtime: hooks fire in the correct order |
| `App::ICheatManager::Get`, `IGameModeManager::Get` | 0x67DDE0 / 0x67DCD0 | runtime (cheats, mode ids) |

## 5.4 Structures (offsets from `cCreatureAnimal*`)

| Field | Offset | Note |
|---|---|---|
| cSpatialObject / cLocomotiveObject | +0xC0 | vtable: GetPosition 0x2C, SetPosition 0x38, SetOrientation 0x3C, Teleport 0x44, SetDesiredSpeed 0xC4, MoveTo 0xE0, StopMovement 0xEC |
| mPosition / mOrientation | +0xC4 / +0xD0 | the planet is a sphere of radius ~505 |
| mVelocity | +0x288 | |
| cCombatant | +0x5A8 | HP +0x5E0, MaxHP +0x5D4 (MaxHP reads as 1.0 - to be clarified) |
| mpSpeciesProfile / mSpeciesKey | +0xB20 / +0xB28 | |
| flags | +0xB58 | bit 0x200 = avatar |
| mHerd | +0x1674 | **required**, see 5.5 |
| `cGameNounManager` | | mpAvatar +0x54, mpAvatarHerd +0x58, mPosseMembers +0x5C |

Mode IDs: Creature 0x1654C01, Editor 0xDBDBA1, main menu (GalaxyGameEntry) 0x2CCD1D2.
Cheat console: `ICheatManager` vtable AddCheat 0x18, RemoveCheat 0x1C, ProcessLine 0x20, GetArgScript 0x38.
`ArgScript::Line`: `vector<char*> mArguments` +0x14, mFirstArgIndex +0x3C, mArgumentCount +0x40.
`ArgScript::ICommand` (MSVC vtable): ParseLine, GetDescription, SetData, AddRef, Release, ~dtor;
fields: FormatParser* +4, refcount +8, data +0xC.

## 5.5 Problems found and solutions

* **A herd is required.** Code at 0x00D41A20 iterates over a creature's neighbors and calls
  `GetHerd()` on each without a null check → for a creature without a herd every update crashes with an AV
  at address 0x15C (Wine swallows the exception, the frame is aborted, the game "freezes").
  Solution: puppets are created in the avatar's herd (`mpAvatarHerd`) - which semantically is the posse anyway.
* **MinHook under Wine.** When installing a hook, MinHook suspends all threads of the process;
  a suspended thread may hold ntdll's virtual memory lock, and the next
  `VirtualProtect` hangs forever (reproduced when launching a second copy of the game).
  Solution: thread freezing in MinHook is disabled (`MH_SetFreezeThreads`), hooks are installed from
  the main thread.
* **Sockets in a second Wine process.** `select()`/blocking TCP operations could
  hang for tens of seconds in a second copy of the game under the same wineserver. The console was rewritten
  to use non-blocking sockets with polling.
* **Pausing in the background.** Spore stops rendering without focus; fullscreen mode under Wine
  minimizes. Solution: SporeKit's "background mode" (swallows WM_ACTIVATE*/KILLFOCUS,
  hooks GetForegroundWindow/GetActiveWindow/GetFocus) and a windowed test profile.

## 5.6 How NPC sync works (M2)

* **Creature registry** - a hook on `cCreatureAnimal::Update`: every creature updated during a frame
  is added to a list; the hook also acts as a filter (on the guest, its own NPCs are not updated and are removed).
* The host assigns NPCs a `net_id` and sends `NPC_SPAWN` (species key, "family" flag), `NPC_STATE` at 10 Hz,
  `NPC_DESPAWN`; the guest creates a proxy via `cCreatureAnimal::Create` in a herd of its species
  (`CreateHerd`, size 0) or in the avatar's herd for "family", without AI (hook on `NPCTickAI`).
* Damage - a hook on `TakeDamage`: a guest's hit on a proxy → `DAMAGE_REQ` → the host applies it to the real
  NPC with the guest's puppet as the attacker; an NPC's hit on a puppet on the host → `DAMAGE` → the guest
  applies it to its avatar. No friendly fire.
* **A dead proxy flies off the planet.** The guest plays a proxy's death locally (`TakeDamage`
  1e6 when the host's state says dead). The body then moves away from the planet centre at
  ~2.5 km/s (measured in the tribe stage, 2026-10-23: the same with damage = health + 1 and with
  the blow coming from above, so it is no knockback; the locomotive velocity +0x288 reads 0).
  Driven like a living proxy it was teleported back every frame and stayed ~80 m in the sky.
  `npc.cpp guest_tick` now pins a killed proxy to the host's corpse (velocity 0 + teleport every
  frame); coop_tribe.py checks the hunted animal's body (old code: 77.7 m off). Why the body
  flies (a ragdoll / death physics without the AI that a puppet lacks?) is open.

* **Time of day** - `cTimeOfDay::SetTimeOfDay` computes a quaternion and calls `SetOrientation`
  on the sky object (`0x01021110()`, then `0x01021630(obj)`); the host sends this object's orientation.
* **DNA** - global `float` 0x0169F378; `AddEvolutionPoints(float)` (0xD2E8A0, cdecl) also
  updates statistics and sends event 0x45AB96E for the UI; the guest catches up to the host's total
  by calling the original `Add` with the difference.

## 5.7 Open questions

* Auto-loading the received world: `cGamePersistenceManager::LoadGame` (0xB271A0, vtable[4],
  the first parameter field is the save name as a `string16`) is **not** called by the menu; the menu's load path
  goes through code near the string `%lslastSave.pld` (0xF94A26), which Ghidra didn't recognize
  as a function.

* Camera/view matrices for `world_to_screen` (nicknames above creatures) - `cGameModeManager::GetViewer`.
* Puppet recoloring (a different color/size to distinguish it from kin).
* Ability animations (attack, social) for M2: `cCreatureBase::PlayAnimation`, abilities via
  `GetAbility(index)` (vtable 0xB4).
* Full exe analysis in REA (requires a `REA_GHIDRA_STARTUP_TIMEOUT_MS` timeout of ≥ 2 h and
  reconnecting the MCP server).

## Where a slow frame goes: coop.sample (2026-10-31, fix/w26-cell-spare)
<a id="re-notes-sample"></a>

* `coop.sample [ms] [top]` in a running game: a sampling profile of the main thread (suspend, read
  EIP and the top of the stack, resume, every ~1 ms; `self:` and `stack:` lists at 0x400000
  addresses, `outside_exe=` the samples in DLLs or Wine, e.g. waiting for the GPU). Map the addresses
  with `tools/ghidra/q.sh --build 2024` (`info` / `dec`). Details and the pitfall (it must not run on
  the main thread) in docs/06-testing.md#testing-cell-spare.
* For "who calls X and with what result" a counter keyed by `__builtin_return_address(0)` in the
  hook is quicker than the sampler (BUG-0355: DestroyCell by caller, ParticleAlloc by call site);
  `coop_module_of()` (debug.cpp) prints module+offset for addresses outside the exe.
