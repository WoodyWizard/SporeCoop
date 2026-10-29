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
| Game | **Spore + Galactic Adventures**; tested with the **SPORE Collection from GOG** (2016 and 2024 builds, see section 2). The mod runs the Galactic Adventures executable `SporebinEP1\SporeApp.exe`. |
| System | Windows 10/11, or Linux with Wine 9+ (Bottles, Lutris, plain Wine; Proton works if the game runs from a GOG install). |
| Players | 2–4, every player with the **same SporeCoop version** (the network protocol is checked on join). |
| Network | The same local network, a virtual LAN (Tailscale, ZeroTier, Radmin VPN), or UDP port 47810 forwarded to the host. |

The mod is tested with the GOG builds (2016 on Linux, 2024 on Windows 11): the full list is in
[section 2](#where-sporecoop-has-been-tested). Other builds (Steam, EA app, disc) have not been
tested yet; if you try one, an [issue](https://github.com/WoodyWizard/SporeCoop/issues) with the
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
Steam's own game folder is `steamapps\common\<Spore folder>`: install into its `SporebinEP1` as for GOG.

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
3. It shows whether your game build is the tested one and which SporeCoop version is installed.
   Press **Install**. The setup downloads the newest release from GitHub and checks it.
4. If the game is under `C:\Program Files (x86)`, the setup offers to restart itself as
   administrator (Windows does not let normal programs write there).
5. Start SPORE as usual.

The same window updates SporeCoop (**Update to x.y.z**) and removes it (**Uninstall**). Without
internet, run the `SporeCoopSetup.exe` from the release zip: it installs the update pack
(`SporeCoop-<version>.pack`) lying next to it.

### With install.bat

1. Download `SporeCoop-<version>.zip` from the
   [latest release](https://github.com/WoodyWizard/SporeCoop/releases/latest).
2. Open the game folder and go into **`SporebinEP1`** (the folder with `SporeApp.exe`).
3. Extract the zip there, so that `SporebinEP1` now contains `install.bat` and a `SporeCoop` folder.
4. Run **`install.bat`**. If the game is under `C:\Program Files (x86)`, right-click it and choose
   **Run as administrator** (Windows does not let normal programs write there).
5. Start SPORE as usual (from GOG Galaxy or the desktop shortcut).

Some antivirus programs warn about a replaced `steam_api.dll`: that file is the SporeCoop
loader. Its source is in this repository (`loader/loader.c`); the release is built by
GitHub Actions from that source.

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
* Start a game, press **F9**: the co-op window opens.

If there is no Co-op item, see [Troubleshooting](#9-troubleshooting).

## 7. Playing over the internet

The host listens on **UDP port 47810**. Guests need to reach it:

* **Easiest:** all players join the same virtual LAN:
  [Tailscale](https://tailscale.com), [ZeroTier](https://www.zerotier.com) or
  [Radmin VPN](https://www.radmin-vpn.com). Then join with the host's VPN address
  (the co-op window shows the host's addresses), or pick the host under *Games on your network*.
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

**Uninstall:** in `SporeCoopSetup.exe` press **Uninstall**, or run `uninstall.bat` in `SporebinEP1`. It puts the original `steam_api.dll` back and
removes `sporekit.dll` and `Data\SporeCoop.package`; delete the `mods` folder too if you like.
By hand: delete `steam_api.dll`, rename `steam_api_orig.dll` back to `steam_api.dll`, delete
`sporekit.dll`, `mods\` and `..\Data\SporeCoop.package`.

**Your saves:** a guest stores the host's world as a separate save slot (marked as a co-op world)
and reuses it next time. If you already have your own save of the same planet, it is moved to
`Games-sporecoop-backup` next to your `Games` folder first, never overwritten.

## 9. Troubleshooting

Stuck? Ask in the [SporeCoop Discord](https://discord.gg/7Yre7u4b8x).

| Problem | What to do |
|---|---|
| No **Co-op** item in the menu | Check the build (section 2). Check that `steam_api.dll` in `SporebinEP1` is the SporeCoop one (about 8 KB) and `steam_api_orig.dll` exists. Look at `mods\logs\sporekit.log`. If it says `WATCHDOG (no frames` while the game draws, another overlay (Steam, Discord, RivaTuner) keeps the frames from SporeKit: update to 0.7.2 or newer, which then takes the frames from the game loop. |
| The game does not start after installing | Run `uninstall.bat`, start the game once without the mod, then install again. An antivirus may have removed `steam_api.dll` or `sporekit.dll`: restore them or add an exception. |
| *Games on your network* stays empty | Both PCs must be on the same network or VPN; allow SporeApp.exe in the firewall (private networks). Type the host's address instead. |
| Join fails or times out | The host's UDP 47810 must be reachable (VPN or port forwarding). Every player needs the same SporeCoop version. |
| The guest loads the world and then nothing | In Space the host must be in a star system, not on a planet (the game only saves off-planet). |
| The game crashed or froze | Since 0.7.2.1 SporeCoop packs a report by itself when the game crashes, freezes for 30 s or closes right after an error: `mods\logs\SporeCoop-report-<date-time>.zip`, and the next start of the game tells you its name. Send that one file in an [issue](https://github.com/WoodyWizard/SporeCoop/issues). (Inside: the crash report `crash-<date-time>.txt` and its `.dmp`, the logs; the logs of the last 10 runs stay in `mods\logs\previous\`.) |
| Something else | In the co-op window (F9) press **Report a problem**: it packs everything into one file, `mods\logs\SporeCoop-report-<date-time>.zip` (a report with your game build, Windows / Wine, memory, graphics card, other overlays and the co-op session; the logs of this and the last runs; the newest crash reports; the mod settings), and opens the folder with it selected. Send that one file (from host and guest) in an [issue](https://github.com/WoodyWizard/SporeCoop/issues) and say what you did. Your player name is in it; the address you last joined is not. The window's bottom line and the main menu show your SporeCoop version. |
| Something goes wrong while you play (only one of you can eat, a partner looks wrong, things out of sync) | Type `/bug <what you see>` into the co-op window's chat (F9) right when it happens, e.g. `/bug only I can eat`. The game says "marked for the report" and every player's log gets a line with your text and the moment, so we find that moment in all the logs. Then press **Report a problem** (each player) and send the files. |
