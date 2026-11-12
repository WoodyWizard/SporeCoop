# Installing SporeCoop

Every player installs the same SporeCoop version into their own copy of the game. The install
replaces one file of the game (`steam_api.dll`, kept as `steam_api_orig.dll`) and adds a few
others; your saves and creations are not touched.

* [1. What you need](#1-what-you-need)
* [2. Check your game build](#2-check-your-game-build)
* [3. Install on Windows](#3-install-on-windows)
* [4. Install on Linux (Wine, Bottles, Lutris, Proton)](#4-install-on-linux-wine-bottles-lutris-proton)
* [5. Install by hand](#5-install-by-hand)
* [6. Check that it works](#6-check-that-it-works)
* [7. Playing over the internet](#7-playing-over-the-internet)
* [8. Update and uninstall](#8-update-and-uninstall)
* [9. Troubleshooting](#9-troubleshooting)

## 1. What you need

| | |
|---|---|
| Game | **Spore + Galactic Adventures**; the current **GOG** download (2024 update) or **Steam**, see section 2. The mod runs the Galactic Adventures executable `SporebinEP1\SporeApp.exe`. |
| System | Windows 10/11, or Linux with Wine 9+ (Bottles, Lutris, plain Wine; Proton works if the game runs from a GOG install). |
| Players | 2–4, every player with the **same SporeCoop version** (the network protocol is checked on join). |
| Network | The same local network, a virtual LAN (Tailscale, ZeroTier, Radmin VPN), or UDP port 47810 forwarded to the host. |

Supported: the current GOG build (the 2024 update) and Steam, on Windows and Linux; the GOG 2016
build is no longer supported since 0.7.8, the EA app and disc versions are not supported. Details:
[compatibility details](#compatibility-details), every test: [section 2](#where-sporecoop-has-been-tested).
If you try another build, an [issue](https://github.com/WoodyWizard/SporeCoop/issues) with the
result and `mods\logs\sporekit.log` helps.

## 2. Check your game build

SporeCoop has the game addresses for these builds of `SPORE\SporebinEP1\SporeApp.exe`:

| Build | Size | SHA-256 |
|---|---|---|
| GOG 2016 (the build SporeCoop was developed on) | 24 888 320 bytes | `922af7c7908af2614b9caca2ffb1ec2353442022fd124866105724acaa24c26e` |
| GOG 2024 update (the current GOG download) | 24 895 536 bytes | `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37` |
| Steam (since its October 2024 update; Spore + Galactic Adventures) | 25 066 744 bytes | `3010a413f9a4a05ee9c4de24756c1c94c7fb218f70cad29bcf5bc0b4abfa2466` |

The Steam `SporeApp.exe` is the GOG 2024 build with Steam's DRM wrapper (the game code is encrypted
in the file and unpacked when the game starts). Since 0.7.1 SporeCoop recognises it, waits for the
unpacked code, checks that it is the GOG 2024 code and then uses the GOG 2024 addresses: Steam and
GOG 2024 players can play together. The log says "the unpacked code is the code of build dc04aee5...".
Steam installs Spore and Galactic Adventures as two apps in two folders next to each other: base
Spore (app 17390) in `steamapps\common\Spore` (only `SporeBin`, no `SporebinEP1`) and Galactic
Adventures (app 24720) in **`steamapps\common\Spore 24720`** (Steam may write it `spore 24720`), which
has `SporebinEP1`. SporeCoop goes into `Spore 24720`: choose that folder in the setup (setups newer
than 0.7.3.3 find it by themselves, also when you choose `Spore` or its `SporeBin`).

**0.7.x is the last version for the GOG 2016 build.** From 0.8 SporeCoop supports only the current
GOG build (the 2024 update; GOG Galaxy and the GOG installers give you that one): with the 2016
build co-op stays off. Update the game, or stay on SporeCoop 0.7.

Players of one game must have the same build: the host turns away a guest with another
`SporeApp.exe`. With any other build the game starts normally but without co-op
(`mods\logs\sporekit.log` says "no symbol database for this SporeApp.exe build").

Since 0.6.2 an exe that differs from one of these only in the PE header, the resources or a
signature (the same code and data at the same addresses: the same `code_sha256`, see
`tools/exe_info.py`) uses that build's addresses, and counts as that build for joining. The log
then says "its code is the same as build ...".

### Compatibility details

SporeCoop runs the Galactic Adventures game, `SporebinEP1\SporeApp.exe`. Where it has been tested:

| Game version | `SporeApp.exe` | Windows 11 | Linux (Wine / Proton) |
|---|---|---|---|
| **GOG**, the current download (2024 update) | 24 895 536 bytes, `dc04aee5…` | ✅ 0.6.1: every stage as host + guest; 0.7.1, 0.7.2: with a Steam player | ✅ 0.7.0: the full automated suite (every stage, every Space feature) |
| **Steam**, Spore + Galactic Adventures (October 2024 update) | 25 066 744 bytes, `3010a413…` | ✅ 0.7.1, 0.7.2: Steam guest + GOG host (Creature), Steam offline ¹ | ✅ 0.7.1: Steam + Proton, offline |
| **GOG 2016**, the older GOG download | 24 888 320 bytes, `922af7c7…` | ❌ no longer supported since 0.7.8 ² | ❌ no longer supported since 0.7.8 ² |
| EA app / Origin, disc | other files | ❌ not supported yet | ❌ not supported yet |

¹ With Steam **online** its overlay can keep the game's frames from the mod (no *Co-op* item,
[issue #4](https://github.com/WoodyWizard/SporeCoop/issues/4)). Since 0.7.2 the mod then takes its
frames from the game loop; tested by simulating that case, not yet with the real overlay.
² **GOG 2016 is no longer supported since 0.7.8** (2026-11-05): update the game through GOG (the current
download is the 2024 build; Steam is the same game). The mod still starts on GOG 2016, but it is not
tested there any more and its bugs are not fixed. Since 0.7.2.2 new fixes get game addresses for
the current build only: on GOG 2016 they switch themselves off (Alt+Shift+Enter, the hatchling growing
up after a death, the pack, Cell bites, meat and spin, NPC nations' vehicles on a guest's game, our
creatures kept out of saves); the other fixes work there too. The full list, checked by the build:
[data/symbols/frozen_off_922af7c7.txt](../data/symbols/frozen_off_922af7c7.txt).

**Playing together:** GOG 2024 and Steam players can play together (the same game code). A GOG
2016 player cannot join a GOG 2024 or Steam host (the host turns away another build). Every player
needs the same SporeCoop protocol: every 0.7 version plays with every other, 0.6.x cannot join 0.7.
With an older 0.7 version (0.7.2.x, 0.7.3.0) on one side, chat lines above about 1160 bytes arrive
cut and the Creature species table of a planet with 42 or more species does not reach that player;
the game names that player in a notification. Update everybody to the latest version.

**Not tested yet:** two PCs over the internet with Windows on 0.7 (the network code is the same
as on Linux, where it was tested), the EA app and disc versions. Tried another version? Please
tell us in an [issue](https://github.com/WoodyWizard/SporeCoop/issues) with `mods\logs\sporekit.log`.

### Where SporeCoop has been tested

| Game build | System | Tested |
|---|---|---|
| GOG 2016 (`922af7c7…`) | Linux, Wine / Bottles | Yes: SporeCoop was developed on it; every stage, 2-4 players, the stability runs of 0.6.0 |
| GOG 2024 (`dc04aee5…`) | Windows 11 | Yes (0.6.1): every stage (cell, creature, tribe, civilization, space) as host + guest, two game instances on one PC |
| GOG 2024 with a changed header (same `code_sha256`) | Windows 11 | Only with a modified copy of the GOG 2024 exe (0.6.2): it loads the GOG 2024 addresses and joins a GOG 2024 host |
| GOG 2016 | Windows | Not tested. Before 0.6.1 every address was wrong on Windows (it moves the exe in memory), so 0.6.0 and older could not work there |
| GOG 2024 (`dc04aee5…`) | Linux, Wine / Bottles | Yes (0.7.0): the full automated suite (every stage, every Space feature, host + guest, real clicks) on copies of the current GOG install; before that a smoke test of the 0.6.2 release zip |
| Steam (`3010a413…`) | Linux, Steam + Proton (offline mode) | Yes (0.7.1): the mod recognises the unpacked code, every feature is on, a Steam guest joined a GOG 2024 host in the Creature stage (world transfer, avatar, the host's NPCs), no faults |
| Steam | Windows 11, Steam offline | Yes (0.7.1, 0.7.2): every feature on; a Steam guest played with a GOG 2024 host in the Creature stage (world transfer, puppets, NPC damage, shared DNA), also with the frames from the game loop (`frames=loop`) as with the Steam overlay online; not tested with Steam online |
| EA app / Origin, disc | any | Not tested. Their `SporeApp.exe` is a different file; the game runs, co-op stays off unless its code matches a build above |
| Two different PCs over the internet or a VPN | Windows | Not tested with 0.6.1 on Windows (the network code itself is the same as on Linux, where it was tested) |

If you try a build or system that is not tested, an
[issue](https://github.com/WoodyWizard/SporeCoop/issues) with the result and
`mods\logs\sporekit.log` (it names the exe's `sha256` and `code_sha256`) helps.

Where the game usually is:

* GOG Galaxy: `C:\Program Files (x86)\GOG Galaxy\Games\SPORE`
* GOG offline installer: `C:\GOG Games\SPORE`
* Linux: inside the Wine prefix, e.g. `drive_c/GOG Games/SPORE`, or wherever you installed it.

Windows (PowerShell, in the `SporebinEP1` folder):

```powershell
Get-FileHash .\SporeApp.exe -Algorithm SHA256
```

Linux:

```bash
sha256sum "SPORE/SporebinEP1/SporeApp.exe"
```

`install.bat` checks this too and tells you when your build is not one of these.

## 3. Install on Windows

### With the setup (recommended)

1. Download **`SporeCoopSetup.exe`** from the
   [latest release](https://github.com/WoodyWizard/SporeCoop/releases/latest) and run it.
2. It finds the game by itself (GOG and EA registry entries, common folders); if not, press
   **Browse...** and choose the SPORE folder (the one with `SporebinEP1`).
   **Steam:** Galactic Adventures has its own folder, **`steamapps\common\Spore 24720`**: choose that
   one, not `steamapps\common\Spore` (base Spore, no `SporebinEP1`) and not `SporeBin`. No
   `Spore 24720` folder means Galactic Adventures is not installed: install the *Spore Galactic
   Adventures* DLC first, SporeCoop needs it.
   Setups newer than 0.7.3.3 also search Steam's library folders, take the folder above a chosen
   `SporeBin` / `SporebinEP1`, switch to `Spore 24720` by themselves ("Galactic Adventures is installed
   in ...\Spore 24720: using that folder") and say **Galactic Adventures is not installed** when no
   folder has it.
3. It shows whether your game build is the tested one and which SporeCoop version is installed.
   Press **Install**. The setup downloads the newest release from GitHub and checks it.
4. If the game is under `C:\Program Files (x86)`, the setup offers to restart itself as
   administrator (Windows does not let normal programs write there).
5. Start SPORE as usual.

The same window updates SporeCoop (**Update to x.y.z**) and removes it (**Uninstall**). Without
internet, run the `SporeCoopSetup.exe` from the release zip: it installs the update pack
(`SporeCoop-<version>.pack`) lying next to it.

The setup and the in-game update ask GitHub's API for the newest release. GitHub allows 60 such
requests an hour per IP address without a login, so on a shared address or a VPN the API can answer
403 or 429. Versions after 0.7.3.3 then find the release through its web page instead (the window
shows "Newest release: SporeCoop x.y.z (from the release page)"); the download is checked against
the release's `SHA256SUMS.txt` either way.

### With install.bat

1. Download `SporeCoop-<version>.zip` from the
   [latest release](https://github.com/WoodyWizard/SporeCoop/releases/latest).
2. Open the game folder and go into **`SporebinEP1`** (the folder with `SporeApp.exe`).
3. Extract the zip there, so that `SporebinEP1` now contains `install.bat` and a `SporeCoop` folder.
4. Run **`install.bat`**. If the game is under `C:\Program Files (x86)`, right-click it and choose
   **Run as administrator** (Windows does not let normal programs write there).
5. Start SPORE as usual (from GOG Galaxy or the desktop shortcut).

Some antivirus programs warn about a replaced `steam_api.dll`: that file is the SporeCoop
loader. See [Antivirus warnings and verifying a release](#install-antivirus).

### Antivirus warnings and verifying a release
<a id="install-antivirus"></a>

Some antivirus programs (on VirusTotal about 10 of 70, among them Microsoft Defender's
machine-learning detection `Wacatac.B!ml`, Bitdefender, 360 Total Security) flag `SporeCoopSetup.exe`,
`steam_api.sporekit.dll` or the release zip (GitHub #200, #201). These are **false positives** of
heuristics: SporeCoop does what such heuristics look for, openly and for a reason:

| What SporeCoop does | Why |
|---|---|
| Its loader takes the name `steam_api.dll` and keeps the game's file as `steam_api_orig.dll` ("DLL side-loading") | Spore loads `steam_api.dll` at start: the loader forwards every Steam call to the original and loads SporeKit. This is how the mod gets into the game. |
| `sporekit.dll` and `coop.dll` hook game and Direct3D functions (MinHook) | That is what a game mod is: co-op needs the game's own functions. |
| `coop.dll` opens network sockets | The co-op connection to the other players (UDP 47810) and LAN discovery. |
| The setup and the in-game update download a file from GitHub and replace DLLs | Updates come only from this repository's GitHub releases and are checked against the release's `SHA256SUMS.txt` before anything is written. |
| The setup can restart itself as administrator | Only when Windows refuses to write into the game folder (`C:\Program Files (x86)`), and only after you say yes. |
| The files are not code-signed | A code-signing certificate is not free; signing is planned. |

The source of every file is in this repository and every release is built by GitHub Actions from the
tag (the [workflow](../.github/workflows/build.yml)): nobody uploads binaries by hand. What each file is
and does: [SECURITY.md](../SECURITY.md).

**Check that your files are the released ones:**

1. **Hashes:** each release has `SHA256SUMS.txt`. On Windows (PowerShell):
   `Get-FileHash .\SporeCoopSetup.exe -Algorithm SHA256`; on Linux: `sha256sum SporeCoopSetup.exe`.
   The value must be the one in `SHA256SUMS.txt`.
2. **Build provenance** (releases after 0.7.12): GitHub signs a record of which workflow run built each
   file from which commit. With the [GitHub CLI](https://cli.github.com/):
   `gh attestation verify SporeCoopSetup.exe --repo WoodyWizard/SporeCoop`. This works for the zip, the
   pack, the setup and the three DLLs inside the zip (`steam_api.sporekit.dll`, `sporekit.dll`,
   `mods\coop.dll`). The release page on GitHub links the attestations too.
3. **File details:** releases after 0.7.12 carry version information (Explorer: right-click > Properties >
   Details): product SporeCoop, the version, a description and the original file name.
4. **Build it yourself** (Linux, llvm-mingw `20261006` as in the workflow, see [Building from source](../README.md#building-from-source)): `make` and
   `tools/package.sh` on the release tag. The binaries differ only in the link time stamp and the debug
   record's id; `tools/pe_version.py --repro-sha256 <file>` hashes a file without those fields, so your
   build and the released file (stripped like `tools/package.sh` does) give the same value.

**Install without the setup:** if your antivirus blocks `SporeCoopSetup.exe`, use the zip and
`install.bat` (*With install.bat* above): a plain batch file you can read in Notepad before you run it.
Your antivirus may still flag the DLLs; then add an exception for the `SporebinEP1` folder only after
checking the hashes, or report the file to your antivirus vendor as a false positive (most have a form
for it). Please do not turn your antivirus off.

## 4. Install on Linux (Wine, Bottles, Lutris, Proton)

The files are the same as on Windows. The easiest way: run **`SporeCoopSetup.exe`** in the game's
Wine prefix (Bottles: *Run Executable...*; Lutris: *Run EXE inside Wine prefix*; plain Wine:
`WINEPREFIX=~/.wine wine SporeCoopSetup.exe`). If it does not find the game, point **Browse...** at
the SPORE folder inside the prefix.

Or run `install.bat`, one of these ways:

**Plain Wine** (use your prefix):

```bash
cd "/path/to/SPORE/SporebinEP1"
unzip ~/Downloads/SporeCoop-*.zip
WINEPREFIX=~/.wine wine cmd /c install.bat
```

**Bottles:** open the bottle that has SPORE → *Run Executable…* → choose `install.bat` in
`SporebinEP1`. Or from a terminal:
`flatpak run --command=bottles-cli com.usebottles.bottles run -b "<bottle>" -e "/path/to/SPORE/SporebinEP1/install.bat"`

**Lutris:** right-click the game → *Run EXE inside Wine prefix* → `install.bat`.

**Without running anything:** do the [install by hand](#5-install-by-hand) with your file manager.

`steam_api.dll` is loaded from the game folder, so no DLL override is needed.

## 5. Install by hand

Inside `SporebinEP1`, after extracting the zip:

| Do | From | To |
|---|---|---|
| rename (once, keep it!) | `steam_api.dll` | `steam_api_orig.dll` |
| copy | `SporeCoop\steam_api.sporekit.dll` | `steam_api.dll` |
| copy | `SporeCoop\sporekit.dll` | `sporekit.dll` |
| copy the folder contents | `SporeCoop\mods\` | `mods\` |
| copy | `SporeCoop\SporeCoop.package` | `..\Data\SporeCoop.package` |

The result:

```
SPORE\
├── Data\SporeCoop.package        the "Co-op" menu icon and notification icons
└── SporebinEP1\
    ├── SporeApp.exe              the game (unchanged)
    ├── steam_api.dll             SporeCoop loader
    ├── steam_api_orig.dll        the original steam_api.dll (the loader passes calls to it)
    ├── sporekit.dll              SporeKit: symbols, hooks, console
    └── mods\
        ├── coop.dll              the co-op mod
        ├── coop.ini, sporekit.ini
        ├── sporekit\symbols\*.sym   game function addresses
        └── logs\                 created on the first start
```

## 6. Check that it works

* The main menu (galaxy screen) has a new **Co-op** item on the left.
* `SporebinEP1\mods\logs\sporekit.log` exists and has a line `loaded coop v<version> (gen 1)` with the version you installed (for example `loaded coop v0.7.3.0 (gen 1)`).
* Click **Co-op**: the co-op window opens and asks for your name the first time, then offers *Host a game* and
  *Join a game*. In a game, **F9** opens it.

If there is no Co-op item, see [Troubleshooting](#9-troubleshooting).

## 7. Playing over the internet

The host listens on **UDP port 47810**. Guests need to reach it:

* **Easiest:** all players join the same virtual LAN:
  [Tailscale](https://tailscale.com), [ZeroTier](https://www.zerotier.com) or
  [Radmin VPN](https://www.radmin-vpn.com). Then *Join a game* with the host's VPN address or host name
  (the host's window shows its addresses under *Friends connect to*), or pick the host under *Games on your network*.
* **Port forwarding:** forward UDP 47810 on the host's router to the host's PC, allow
  SporeApp.exe in the Windows firewall, and give friends your public IP.

Connections across a long distance work (the protocol was tested with 120 ms ± 60 ms delay and
3 % packet loss); very lossy links can make a rejoin slow (see `docs/known-issues.md`).

## 8. Update and uninstall

**Update in the game:** after the start SporeCoop asks GitHub whether a newer release exists. If so,
a notification says so and the **Co-op** window shows an **Update** button: the new files are
downloaded, checked against the release's SHA-256 sums and installed; restart Spore to use them.
All players update together (the network protocol has to match).

The check sends one request to `api.github.com` per start and nothing else. To turn it off, set
in `SporebinEP1\mods\coop.ini`:

```ini
[update]
check=0
```

**Update with the setup:** run `SporeCoopSetup.exe` and press **Update to x.y.z**. **By hand:**
extract the new zip into `SporebinEP1` over the old files and run `install.bat` again. Both keep
your `mods\coop.ini` and `mods\sporekit.ini` (name, last address, `[update] check=0`). An older setup
never installs its older version over a newer one without asking (the button then says **Install
older x.y.z**; silent mode `SporeCoopSetup.exe /S` needs `/downgrade` for that).

**Uninstall:** in `SporeCoopSetup.exe` press **Uninstall**, or run `uninstall.bat` in `SporebinEP1`. It puts the original `steam_api.dll` back
(also when an antivirus removed the loader) and removes `sporekit.dll`, `mods\coop.dll`, SporeCoop's
symbol files and `Data\SporeCoop.package`; your settings and logs in `mods` stay, delete the `mods`
folder too if you like. Since 0.7.13 it stops without changing anything while an update runs, when
`steam_api_orig.dll` is missing (verify the game files in Steam / GOG Galaxy first) or when the game
still holds a file, and it names a file it could not remove (close Spore and press Uninstall again).
`uninstall.bat` runs `SporeCoopSetup.exe` when it lies next to it, and otherwise never puts
`steam_api_orig.dll` over a `steam_api.dll` that is not SporeCoop's loader (for example one Steam put back).
By hand: delete `steam_api.dll`, rename `steam_api_orig.dll` back to `steam_api.dll`, delete
`sporekit.dll`, `mods\` and `..\Data\SporeCoop.package`.

**Your saves:** a guest stores the host's world as a separate save slot (marked as a co-op world)
and reuses it next time. If you already have your own save of the same planet, it is moved to
`Games-sporecoop-backup` next to your `Games` folder first, never overwritten.

## 9. Troubleshooting

Stuck? Ask in the [SporeCoop Discord](https://discord.gg/7Yre7u4b8x).

| Problem | What to do |
|---|---|
| The setup says **Galactic Adventures is not installed** (setups newer than 0.7.3.3) | No Spore folder the setup found has `SporebinEP1` (the Galactic Adventures game). On Steam, Galactic Adventures is its own DLC app and installs into its own folder, `steamapps\common\Spore 24720`, next to base Spore's `steamapps\common\Spore` (which never has `SporebinEP1`): install the *Spore Galactic Adventures* DLC in Steam, then run the setup again or choose `Spore 24720`. On GOG the SPORE Collection has it. |
| The setup says **SporeApp.exe is not in this folder's SporebinEP1** | Choose the SPORE folder itself (the one that holds `SporebinEP1`, `Data`, ...). On Steam it is `steamapps\common\Spore 24720` (Galactic Adventures) in the library where Spore is installed, not `steamapps\common\Spore`. |
| The update or the setup says **the server answered 502 / 503 / 504** or **the download broke off** | A passing problem of GitHub's download server. Since 0.7.11 SporeCoop tries again by itself (3 attempts, the message ends with "after 3 attempts" when all failed); wait a minute and press **Try again** in the co-op window, or run the setup again. |
| The setup says **GitHub not reachable** or **the server answered 403** | GitHub limits requests per IP address (VPNs and shared networks hit it). Versions after 0.7.3.3 fall back to the release page; if that fails too, wait an hour or turn the VPN off, or install by hand: download `SporeCoop-<version>.zip` from the [releases](https://github.com/WoodyWizard/SporeCoop/releases/latest), extract it into `SporebinEP1` and run `install.bat` (section 3, *With install.bat*). The release zip also has a `SporeCoopSetup.exe` that installs the pack next to it without the internet. |
| No **Co-op** item in the menu | Check the build (section 2). Check that `steam_api.dll` in `SporebinEP1` is the SporeCoop one (about 8 KB) and `steam_api_orig.dll` exists. Look at `mods\logs\sporekit.log`. If it says `WATCHDOG (no frames` while the game draws, another overlay (Steam, Discord, RivaTuner) keeps the frames from SporeKit: update to 0.7.2 or newer, which then takes the frames from the game loop. |
| The game crashes at start while **RivaTuner / MSI Afterburner** runs | See [Crash at start with RivaTuner](#install-rtss): an RTSS profile for SporeApp.exe with *Application detection level* None, or close RTSS. |
| The picture **tears** (screen tearing) | See [The picture tears](#install-tearing): `vsync=1` in `mods\sporekit.ini`, or vsync in the driver. |
| The game does not start after installing | Run `uninstall.bat`, start the game once without the mod, then install again. An antivirus may have removed `steam_api.dll` or `sporekit.dll`: restore them or add an exception. |
| *Games on your network* stays empty | Both PCs must be on the same network or VPN; allow SporeApp.exe in the firewall (private networks). Type the host's address or host name into the address field of *Join a game* instead (port 47810 when you type none). |
| Join fails or times out | The host's UDP 47810 must be reachable (VPN or port forwarding). Every player needs the same SporeCoop version. |
| The guest loads the world and then nothing | In Space the host must be in a star system, not on a planet (the game only saves off-planet). |
| The game crashed or froze | Since 0.7.2.1 SporeCoop packs a report by itself when the game crashes, freezes for 30 s or closes right after an error: `mods\logs\SporeCoop-report-<date-time>.zip`, and the next start of the game tells you its name. Send that one file in an [issue](https://github.com/WoodyWizard/SporeCoop/issues). (Inside: the crash report `crash-<date-time>.txt` and its `.dmp`, the logs; the logs of the last 10 runs stay in `mods\logs\previous\`.) |
| Something else | In the co-op window (F9) or the game's pause menu (Esc) press **Report a problem**: it packs everything into one file, `mods\logs\SporeCoop-report-<date-time>.zip` (a report with your game build, Windows / Wine, memory, graphics card, other overlays and the co-op session; the logs of this and the last runs; the newest crash reports; the mod settings), and opens the folder with it selected. Send that one file (from host and guest) in an [issue](https://github.com/WoodyWizard/SporeCoop/issues) and say what you did. Your player name is in it; the address you last joined is not. The window's bottom line and the main menu show your SporeCoop version. |
| Something goes wrong while you play (only one of you can eat, a partner looks wrong, things out of sync) | Type `/bug <what you see>` into the co-op window's chat (F9) right when it happens, e.g. `/bug only I can eat`. The game says "marked for the report" and every player's log gets a line with your text and the moment, so we find that moment in all the logs. Then press **Report a problem** (each player) and send the files. |

### Crash at start with RivaTuner / MSI Afterburner
<a id="install-rtss"></a>

RivaTuner Statistics Server (RTSS, it comes with MSI Afterburner) puts its overlay into games by hooking Direct3D, the
same functions SporeKit hooks; players reported Spore crashing at start while it ran (BUG-0782). Since 0.7.13 the game
tells you when RTSS is hooked in, and a crash report names it with this advice:

1. In RivaTuner, press **Add** and choose `SporebinEP1\SporeApp.exe`, then set **Application detection level** to
   **None** for that profile (RTSS then leaves Spore alone; Afterburner's own monitoring keeps working elsewhere).
2. Or close RivaTuner and MSI Afterburner (also from the tray) before you start Spore.
3. If you want its overlay in Spore, try **Use Microsoft Detours API hooking** in the profile's settings instead, and
   send a report (**Report a problem**) when it still crashes.

### The picture tears (screen tearing)
<a id="install-tearing"></a>

Spore asks Direct3D for no vsync (seen in every log: `CreateDevice asks for: ... interval=0x80000000 (immediate: no
vsync)`), and its own frame cap (the game property kFrameLimitMS, 16 ms per frame in the GOG 2024 / Steam game) only
limits how often the tear shows (BUG-0779). It shows much more when something lifts that cap (the game then runs at
80-140 fps): a mod (for example a Spore ModAPI mod), a d3d9 wrapper next to `SporeApp.exe` (DXVK's `d3d9.dll`,
ReShade) or a driver profile. Since 0.7.13 the game tells you when it runs without its cap, and SporeKit no longer
undoes a frame cap another mod or program sets.

* Turn vsync on: add `vsync=1` under `[sporekit]` in `SporebinEP1\mods\sporekit.ini` and start the game again (the log
  then says `vsync: [sporekit] vsync=1: presentation interval 0x1`). Or force vsync for SporeApp.exe in the graphics
  driver's settings (NVIDIA Control Panel, AMD Software).
* With DXVK as `d3d9.dll` in the game folder: `dxvk.conf` next to it can set `d3d9.presentInterval = 1` (vsync) or
  `d3d9.maxFrameRate = 30` (a cap); the lab checked that DXVK's cap holds with SporeKit and the mod.
* Without vsync, the tearing goes away only with the program that lifted the cap removed (Spore ModAPI or the mod
  that unlocks the frame rate) or its frame cap set.

### Out of memory in big worlds: Large Address Aware (optional)
<a id="install-laa"></a>

Spore is a 32-bit program and gets 2 GB of address space. Very big worlds and adventures can run out of it (a
crash on a failed allocation, BUG-0631, BUG-0621). The *Large Address Aware* flag in `SporeApp.exe`'s header lets
it use 4 GB on 64-bit Windows and under Wine. SporeCoop never sets it by itself. If you want it, `tools/laa.py` in
the [repository](https://github.com/WoodyWizard/SporeCoop) (Python 3, close Spore first) does it with a verified
backup (BUG-0862):

```
python3 tools/laa.py status  "SPORE/SporebinEP1/SporeApp.exe"
python3 tools/laa.py set     "SPORE/SporebinEP1/SporeApp.exe"   # backup: SporeApp.exe.pre-laa (+ .sha256)
python3 tools/laa.py restore "SPORE/SporebinEP1/SporeApp.exe"   # the original back, checked by its hash
```

It changes only the flag and the header checksum; SporeCoop finds its addresses for such an exe as before (by its
`code_sha256`, section 2), and the setup then says "same code as the GOG 2024 build". The Steam exe (with Steam's
DRM wrapper) is refused unless you add `--force`. Not tested in long co-op sessions yet: if the game misbehaves,
`restore` and send a report.
