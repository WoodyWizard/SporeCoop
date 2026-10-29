<p align="center">
  <img src="docs/media/banner.svg" alt="SporeCoop - play every stage of Spore together" width="100%">
</p>

<p align="center">
  <a href="https://github.com/WoodyWizard/SporeCoop/releases/latest"><img src="https://img.shields.io/github/v/release/WoodyWizard/SporeCoop?label=download&color=8ea6ff" alt="Latest release"></a>
  <a href="https://github.com/WoodyWizard/SporeCoop/actions/workflows/build.yml"><img src="https://github.com/WoodyWizard/SporeCoop/actions/workflows/build.yml/badge.svg" alt="Build"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-74c867" alt="MIT license"></a>
  <img src="https://img.shields.io/badge/game-Spore%20GA%20(GOG%20%7C%20Steam)-f0b44a" alt="Spore Galactic Adventures (GOG | Steam)">
  <img src="https://img.shields.io/badge/OS-Windows%20%7C%20Linux-5aa9e6" alt="Windows | Linux">
  <img src="https://img.shields.io/badge/players-2%E2%80%934-ff7a5f" alt="2–4 players">
  <a href="https://discord.gg/7Yre7u4b8x"><img src="https://img.shields.io/badge/Discord-join%20the%20server-5865F2?logo=discord&logoColor=white" alt="Discord"></a>
</p>

<!-- support:top:start -->
<p align="center">
  <a href="https://buymeacoffee.com/chamomilewoods"><img src="https://img.shields.io/badge/Buy%20me%20a%20coffee-chamomilewoods-FFDD00?style=for-the-badge&logo=buymeacoffee&logoColor=black" alt="Buy me a coffee"></a>
</p>
<p align="center"><b>☕ Like this project? <a href="https://buymeacoffee.com/chamomilewoods">Support the author on Buy Me a Coffee</a></b></p>
<!-- support:top:end -->

**SporeCoop** is a multiplayer mod for Spore. Two to four friends evolve **in one world**, from the
tide pool to the galaxy: everybody plays their own creature, the host's world is shared with the
guests, and every stage has mechanics that only work together - revive a knocked-out cell, hunt as
a pack, besiege a city from two sides, repair your partner's ship before it explodes.

<p align="center">
  <img src="docs/media/tribe_host.jpg" alt="Tribe stage: Alice's and Bob's chiefs, a pincer attack notification" width="80%">
</p>

## Contents

- [Features](#features) · [Every stage together](#every-stage-together) · [Install](#install) ·
  [Compatibility](#compatibility) · [How to play](#how-to-play) · [FAQ](#faq) · [Status and roadmap](#status-and-roadmap) ·
  [Community](#community) · [Building from source](#building-from-source) · [Credits](#credits)

## Features

- **Co-op from the main menu.** A *Co-op* item in Spore's own galaxy menu: host a game, find
  friends on your network or type an address, chat and get ready in the lobby. **F9** opens it in game.
- **One world.** The host's save is sent to every guest and loaded automatically. The host
  simulates creatures, tribes, cities and ships; guests see the same world and act through the host.
- **Your own creations.** Everybody plays their own creature, cell and spaceship from
  *My Creations*, and the others see it.
- **Evolve together.** Entering an editor or the next stage starts a vote; it begins when everybody
  is ready, and the evolved world goes to everybody.
- **Native look.** Windows, fonts and slide-in notifications use Spore's own interface.
- **Safe for your saves.** A guest keeps the host's world in a separate slot; your own saves are
  never overwritten.

<table>
  <tr>
    <td width="50%"><img src="docs/media/ui_lobby_menu.jpg" alt="The co-op window in the galaxy menu: hosting a game"></td>
    <td width="50%"><img src="docs/media/creature_guest_window.jpg" alt="The co-op lobby in game (F9): players, stage, ping, ready, own creature"></td>
  </tr>
  <tr>
    <td>The <i>Co-op</i> window in the galaxy menu: host a game or join one.</td>
    <td>In game, <b>F9</b>: players, stage, ping, readiness, chat and your own creature.</td>
  </tr>
</table>

## Every stage together

| Stage | What you do together |
|---|---|
| 🦠 **Cell: Symbiosis** | One tide pool. A killing bite only knocks you out for 10 s: a partner who swims right next to you revives you, otherwise you respawn as a small cell next to them. |
| 🦎 **Creature: Pack** | Hunt the same animals for +25 % damage; when you are about to die near a partner, the pack saves you; partners heal each other. |
| 🔥 **Tribe: Chiefs** | The tribe's members are split between players, each with their own chief (+20 % aura). Attack a tribe from two sides for +50 % (a *pincer*), perform together to make friends faster, kill the epic creature together for 80 food. |
| 🏙️ **Civilization: Governors** | Every player governs their own cities and vehicles and picks a role: military, economic or religious. Two players besieging one city take it 50 % faster; a city under attack calls the others; superweapons fire only after a vote. |
| 🚀 **Space: A galaxy together** | Each of you rules your own territory in one galaxy. See your partners on the galaxy map and fly to them with a click, defend each other's colonies, trade spice and give tool charges, take co-op contracts (a pirate armada that needs two ships, joint surveys), share one diplomacy toward the NPC empires and reach the galactic core together. A destroyed ship next to a partner is only disabled for 20 s. Or play as **rivals**: own budgets, alliances, wars, ship against ship. |

<table>
  <tr>
    <td width="50%"><img src="docs/media/cell_host.jpg" alt="Cell stage: Bob's cell is knocked out, translucent, with a KO countdown; Alice is next to him"></td>
    <td width="50%"><img src="docs/media/creature_host.jpg" alt="Creature stage: Alice and Bob's creatures in one pack"></td>
  </tr>
  <tr>
    <td><b>Cell.</b> Bob is knocked out: Alice has 10 s to swim right next to him and revive him.</td>
    <td><b>Creature.</b> Two players' own creatures in one pack, names above them.</td>
  </tr>
  <tr>
    <td><img src="docs/media/civ_host.jpg" alt="Civilization stage: the capital is under attack, everybody is told"></td>
    <td><img src="docs/media/space_host.jpg" alt="Space stage: Bob's ship is disabled, a REPAIR beacon shows where it is"></td>
  </tr>
  <tr>
    <td><b>Civilization.</b> A city is under attack: every player gets the alarm and a beacon.</td>
    <td><b>Space.</b> Bob's ship is disabled - a <i>REPAIR</i> beacon and a 20 s countdown.</td>
  </tr>
</table>

## Install

You need **Spore with Galactic Adventures** from **GOG** or **Steam**, on Windows or on Linux with
Wine (Bottles, Lutris, Proton). Which versions are tested: [Compatibility](#compatibility). Every
player installs the same SporeCoop version.

1. Download **`SporeCoopSetup.exe`** from [Releases](https://github.com/WoodyWizard/SporeCoop/releases/latest)
   and run it (on Linux: inside the game's Wine prefix, e.g. Bottles *Run Executable...*).
2. It finds the game (GOG or Steam), shows which build you have, and **Install** downloads the
   newest release from GitHub and checks it. Steam: if it does not find the game, choose the
   Spore folder in `steamapps\common` (the one with `SporebinEP1`).
3. Start Spore: the galaxy menu now has a **Co-op** item.

**Updates** come from this repository's releases: SporeCoop checks for a new version when the game
starts, and the *Co-op* window offers an **Update** button (`[update] check=0` in `mods\coop.ini`
turns the check off). The setup updates and uninstalls too. Prefer files? The release zip has
`install.bat` / `uninstall.bat`.

The full guide (checking your game build, Linux, installing by hand, the internet, updating,
uninstalling, troubleshooting) is in **[docs/INSTALL.md](docs/INSTALL.md)**.

## Compatibility

SporeCoop runs the Galactic Adventures game, `SporebinEP1\SporeApp.exe`. Where it has been tested:

| Game version | `SporeApp.exe` | Windows 11 | Linux (Wine / Proton) |
|---|---|---|---|
| **GOG**, the current download (2024 update) | 24 895 536 bytes, `dc04aee5…` | ✅ 0.6.1: every stage as host + guest; 0.7.1, 0.7.2: with a Steam player | ✅ 0.7.0: the full automated suite (every stage, every Space feature) |
| **Steam**, Spore + Galactic Adventures (October 2024 update) | 25 066 744 bytes, `3010a413…` | ✅ 0.7.1, 0.7.2: Steam guest + GOG host (Creature), Steam offline ¹ | ✅ 0.7.1: Steam + Proton, offline |
| **GOG 2016**, the older GOG download | 24 888 320 bytes, `922af7c7…` | ❔ not tested | ✅ up to 0.7.0: SporeCoop was developed on it; the fixes since 0.7.2.2 that need new game addresses are off ² |
| EA app / Origin, disc | other files | ❌ not supported yet | ❌ not supported yet |

¹ With Steam **online** its overlay can keep the game's frames from the mod (no *Co-op* item,
[issue #4](https://github.com/WoodyWizard/SporeCoop/issues/4)). Since 0.7.2 the mod then takes its
frames from the game loop; tested by simulating that case, not yet with the real overlay.
² 0.7.x is the last version for the GOG 2016 build; from 0.8 only the current GOG build (and Steam,
which is the same game): update the game through GOG. Since 0.7.2.2 new fixes get game addresses for
the current build only: on GOG 2016 they switch themselves off (Alt+Shift+Enter, the hatchling growing
up after a death, the pack, Cell bites, meat and spin, NPC nations' vehicles on a guest's game, our
creatures kept out of saves); the other fixes work there too. The full list, checked by the build:
[data/symbols/frozen_off_922af7c7.txt](data/symbols/frozen_off_922af7c7.txt).

**Playing together:** GOG 2024 and Steam players can play together (the same game code). A GOG
2016 player cannot join a GOG 2024 or Steam host (the host turns away another build). Every player
needs the same SporeCoop protocol: every 0.7 version plays with every other, 0.6.x cannot join 0.7.
With an older 0.7 version (0.7.2.x, 0.7.3.0) on one side, chat lines above about 1160 bytes arrive
cut and the Creature species table of a planet with 42 or more species does not reach that player;
the game names that player in a notification. Update everybody to the latest version.

**Not tested yet:** two PCs over the internet with Windows on 0.7 (the network code is the same
as on Linux, where it was tested), the EA app and disc versions. Which build you have:
`SporeCoopSetup.exe` shows it, or see [INSTALL.md, section 2](docs/INSTALL.md#2-check-your-game-build).
Tried another version? Please tell us in an [issue](https://github.com/WoodyWizard/SporeCoop/issues)
with `mods\logs\sporekit.log`.

## How to play

**Host**

1. Galaxy menu → **Co-op** → type your name → **Host**.
2. Load a save or start a new game. The window shows the addresses your friends can use.

**Guest**

1. Galaxy menu → **Co-op** → type your name.
2. Click the host under *Games on your network*, or type the host's address (e.g. `100.64.12.7` on
   Tailscale) → **Join**.
3. The host's world arrives and loads by itself; you appear next to the host with your own creature.

**In game**

| Key / command | What it does |
|---|---|
| **F9** | the co-op window: players, stage, ping, readiness, chat, *Send my world*, *Leave* |
| *I'm ready* | agree to evolve (editor, next stage) |
| `Ctrl+Shift+C` → `coop status` | session details in the cheat console |
| `coop say <text>` | chat |
| `coop ping` | show everybody the place under your mouse (Tribe, Civilization) |
| `coop give` | give your selected tribe members, vehicles or the opened city to a partner |
| `coop role 1`…`3` | Civilization role: military, economic, religious |

**Example: two friends over the internet**

```
Alice (GOG) and Bob (Steam) install Tailscale and the same SporeCoop version.
Alice: Co-op → "Alice" → Host → loads her Tribe save.
       The window says: friends connect to 100.64.12.7:47810
Bob:   Co-op → "Bob" → 100.64.12.7 → Join.
       Alice's tribe loads; Bob gets his share of the members and his own chief.
Both:  F9 → I'm ready when the tribe is ready for Civilization.
```

## FAQ

**Which game versions does it work with?** The current GOG and Steam versions of Spore + Galactic
Adventures, on Windows and Linux: see [Compatibility](#compatibility). The Steam version is the GOG
2024 game under Steam's DRM: SporeCoop waits until the game has unpacked itself and checks the code,
so Steam and GOG players can play together. The EA app and disc versions are not supported yet
(their `SporeApp.exe` is a different file: the game runs, co-op stays off). Every test, build by
build: [INSTALL.md, section 2](docs/INSTALL.md#where-sporecoop-has-been-tested).

**Do all players need the game?** Yes, every player needs their own copy of Spore + Galactic
Adventures (GOG or Steam) and the same SporeCoop version.

**What happens to my saves?** Nothing: the host saves before sending its world; guests keep the
host's world in a separate save slot, and a guest's own save of the same planet is moved to
`Games-sporecoop-backup` first.

**Can we play in different stages?** The host decides the stage; a guest who is somewhere else is
brought to the host's world.

**Does it change the game files?** It replaces `steam_api.dll` with a small loader (the original
is kept as `steam_api_orig.dll` and still used) and adds `sporekit.dll`, a `mods` folder and
`Data\SporeCoop.package`. `uninstall.bat` restores everything.

**Is it safe?** The whole source is here, and the release files are built from it by
[GitHub Actions](https://github.com/WoodyWizard/SporeCoop/actions). The mod listens on UDP 47810 only
while you host; its debug console accepts connections from your own PC only (127.0.0.1). A world
or creature received from another player is checked before it touches the disk: plain file names
inside the save slot only, limited sizes. Updates are downloaded only from this repository's GitHub releases over HTTPS, checked
against the release's SHA-256 sums, and may only replace SporeCoop's own files.

## Status and roadmap

Version 0.7 (the newest release is on the [Releases](https://github.com/WoodyWizard/SporeCoop/releases/latest) page): co-op in every stage for up to 4 players, and a Space stage built for playing
together (the F9 window's **Space** page: partners on the map, allied colonies, trade, contracts,
shared diplomacy, the galactic core together, rivals mode). Tested automatically on real game
instances: 50+ scenario tests with real clicks, bots that play every stage through real input for
hours, 4-player sessions with rejoins, a chaos bot and simulated bad networks
([stability report](docs/stability-report.md)).
Known issues: [docs/known-issues.md](docs/known-issues.md). Found a problem? Type `/bug <what you
see>` in the co-op chat when it happens (it marks that moment in every player's log), then see
[how to report it](docs/REPORTING.md).

Next:

- [ ] easier internet play (no VPN or port forwarding)
- [ ] tests with the EA app build, and of the Steam build with Steam online (overlay)
- [ ] four-player tests of the 0.7 Space features, co-op adventures
- [ ] **Rivals mode**: every player with a base, a species, a tribe, a nation and an empire of its
  own in the same world, a race through every stage with war, peace and alliances
  ([design](docs/11-rivals.md))

## Community

Join the **[SporeCoop Discord](https://discord.gg/7Yre7u4b8x)**: find players to evolve with, ask for help with the setup,
share your worlds and creatures, report problems and suggest ideas. Bug reports with files are
easiest to follow up as a [GitHub issue](https://github.com/WoodyWizard/SporeCoop/issues/new/choose)
([how to report](docs/REPORTING.md)).

## Building from source

SporeCoop is built on Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (32-bit
Windows targets) and tested under Wine/Bottles.

```bash
git clone https://github.com/WoodyWizard/SporeCoop.git   # inside the SPORE game folder
cd SporeCoop
make LLVM_MINGW=/path/to/llvm-mingw   # steam_api.dll (loader), sporekit.dll, coop.dll, SporeCoop.package
make test                             # unit tests (netcode, save format) + symbol check
tools/package.sh                      # out/dist/SporeCoop-<version>.zip
```

| Part | What it is | Where |
|---|---|---|
| loader | `steam_api.dll` proxy: the game loads it and it starts SporeKit | [`loader/`](loader/) |
| SporeKit | runtime inside the game: symbols, hooks, console, input, hot reload, crash and hang logs | [`sporekit/`](sporekit/), [`sdk/`](sdk/include/) |
| coop | the mod: sessions, world sharing, one module per stage, the co-op window | [`mods/coop/`](mods/coop/) |
| symbols | addresses of game functions for this build (2 400+ ported, 240 verified by hand) | [`data/symbols/`](data/symbols/) |
| tests | two-instance scenario tests, chaos bot, bad-network proxy, the parallel test lab | [`tools/tests/`](tools/tests/), [`tools/lab.py`](tools/lab.py) |

Documentation: [architecture](docs/01-architecture.md), [netcode](docs/04-netcode.md),
[reverse engineering](docs/05-re-notes.md) and [per-stage notes](docs/re/),
[testing](docs/06-testing.md), [development pipeline](docs/10-dev-pipeline.md):
all in [docs/](docs/README.md). Contributions are welcome: open an issue first for bigger changes.

## Credits

- Built by [WoodyWizard](https://github.com/WoodyWizard).
- Function names and addresses were ported from [Spore ModAPI](https://github.com/Spore-Community/Spore-ModAPI)
  by the Spore modding community; hooks by [MinHook](https://github.com/TsudaKageyu/minhook).
  Details and licenses: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- Spore is a trademark of Electronic Arts Inc. SporeCoop is an unofficial fan mod, not affiliated
  with or endorsed by Electronic Arts or Maxis. It contains no game files.

Licensed under the [MIT license](LICENSE).

<!-- support:bottom:start -->
## ☕ Support the author

If this project is useful to you, you can support the author - it really helps to keep working on it:

**https://buymeacoffee.com/chamomilewoods**

<a href="https://buymeacoffee.com/chamomilewoods"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" height="50"></a>
<!-- support:bottom:end -->
