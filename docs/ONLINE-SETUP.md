# Online setup record

## Installation investigation (2026-11-07)

Requested: download and install SporeFixOnline and Rosalie241/SporeServer,
preserving a record. Goal: restore automatic online creation downloads for
Lineages. These are separate connection modes, not a combined pollination fix.

### Verified downloads

Downloads and build output are in `out/online-setup/` (ignored by Git).

| Component | Version | Verification |
| --- | --- | --- |
| SporeFixOnline | 1.0.3 | SHA256 matches GitHub release asset digest |
| ModAPI Launcher Kit | 1.5.14 | Setup SHA256 matches GitHub release asset digest |
| SporeServer | 3fd111963ad75aa666095f9d99026ca4bc4c2103 | Source pinned; no upstream binary releases |

SHA256 values:

```text
SporeFixOnline-1.0.3.sporemod
799751b7cec5bc9e71019a8228ed2916ebb776c60aa510e52ca1ea81176c20aa
ModAPI.InterimSetup-1.5.14.exe
475248ed133eba0e47fe4277b69757f76a5909dc3f2476d54c6bee80031969e4
SporeServer-3fd11196.zip (locally recorded, not an upstream signed digest)
6c2b43942cd54c5e0e5d7e02e7e04fbf9d514dfc4d090083aaf4736fb4256afd
```

### Findings and boundaries

- SporeFixOnline updates the EA certificate returned by `SSL::CertStream::Read`.
  It does not implement new creature selection or a replacement catalogue.
- Both client DLLs require ModAPI. They cannot simply be placed in SporeKit's
  `mods` folder: SporeKit expects the `sporekit_mod_main` export.
- Launcher Kit injects modern DLLs from its own `mLibs` folder. Its setup embeds
  the full launcher archive, extracted during this task without running its UI.
- SporeServer is an upstream WIP ASP.NET Core 8 application using MariaDB and
  ASP.NET Identity. Release publish succeeded with installed .NET SDK 9.0.317.
  The published server is in `out/online-setup/server-app`.
- No MySQL/MariaDB Windows service was found. The upstream database configuration
  contains placeholder root credentials and is not ready to run unchanged.
- The replacement server needs its own creation library. It does not provide
  the EA Sporepedia catalogue merely by being installed.
- The SporeServer client can redirect all hostname resolution and HTTPS
  connections in the game, so co-op hostname compatibility needs testing.
  Do not enable it alongside the official-server fix.
- Its INI uses the APPDATA environment variable directly. A SporeKit profile
  redirect alone does not isolate this configuration.
- Steam currently registers base Spore under `Spore 17390/Data` and GA under
  `spore/DataEP1`. Launcher Kit checks for matching parent directories and can
  refuse this setup until Steam Galactic Adventures completes its setup.
- No account login, creation upload, public server, firewall rule, or personal
  save modification was performed.

### Sources

- <https://github.com/Rosalie241/SporeFixOnline/releases/tag/v1.0.3>
- <https://github.com/Rosalie241/SporeFixOnline>
- <https://github.com/Rosalie241/SporeServer/tree/3fd111963ad75aa666095f9d99026ca4bc4c2103>
- <https://github.com/Spore-Community/ModAPI-Launcher-Kit/releases/tag/v1.5.14>
- <https://github.com/Spore-Community/ModAPI-Launcher-Kit>

## Installed state and user decision (2026-11-07)

CORRECTION (2026-11-07): the logical LocalAppData path reported below was
redirected by the packaged Codex application. The actual installed location is:

```text
C:\Users\bandi\AppData\Local\Packages\OpenAI.Codex_2p2nqsd0c76g0\LocalCache\Local\Spore ModAPI Launcher Kit
```

User launched from that location but reported EA login failure. Read-only module
inspection of running Spore PID 32380 confirmed `SporeFixOnline.dll`,
`SporeModAPI.dll`, `SporeModAPI-steam_patched.dll`, `sporekit.dll` and the
Lineages `coop.i1.1.dll` are loaded together. This confirms DLL loading only,
not successful certificate-hook execution, login, or full compatibility.
The earlier statement that runtime injection was unverified is superseded by
this observation. Exact login error and prior account login history requested;
cause remains undiagnosed. Do not move loaded DLLs or change account settings.

User selected: **Use EA; keep SporeServer staged.** No database, replacement
server service, HTTPS certificate, or server redirect is to be activated.

Installed ModAPI Launcher Kit 1.5.14 in:

```text
C:\Users\bandi\AppData\Local\Spore ModAPI Launcher Kit
```

Installation used the verified setup's embedded `ModApiUpdate` archive. Added
Start menu shortcuts for Launcher, Easy Installer and Easy Uninstaller. Wrote
the launcher discovery path to `%APPDATA%/Spore ModAPI Launcher/path.info`.
No prior launcher installation was found at that discovery path or destination.

SporeFixOnline 1.0.3 is installed in `mLibs/SporeFixOnline.dll`. Its original
files are preserved in `ModConfigs/SporeFixOnline`; `InstalledMods.config`
registers its version and DLL for Easy Uninstaller. Installed DLL SHA256:

```text
f1220060efe352bc7d2e0bba75e05a14e3b75ba52fc72bfef3b9928249c5bd93
```

Verification: installed DLL matches the extracted release; installed metadata
parses correctly; bundled ModAPI core versions are 2.5.580, satisfying the
mod's 2.5.20 minimum. No game DLLs, save files or game registry keys were changed.

**Remaining launch prerequisite:** the official launcher's read-only
`SporePath.IsGameInstalled(false)` returned false because Steam's base/GA data
directories are split. Launch Galactic Adventures once through Steam to let
Steam finish its official setup, then close it and launch through **Spore
ModAPI Launcher**. Do not launch base Spore in between. If the warning remains,
inspect the Steam installation paths before changing registry values.

Runtime injection, EA login and automatic pollination are **not yet verified**.
The certificate fix does not guarantee that EA's services or account entitlement
are working. Lineages and ModAPI coexistence also remains unverified at runtime.

### Rollback

Close Spore and use **Spore ModAPI Easy Uninstaller** to remove SporeFixOnline.
Alternatively, move `mLibs/SporeFixOnline.dll` outside the launcher directory to
disable the fix reversibly. Launching GA normally bypasses this separate
ModAPI launcher; the pre-existing Lineages installation remains in place.

SporeServer is staged only in `out/online-setup/source` and
`out/online-setup/server-app`; nothing starts it automatically. Keep its
placeholder database configuration inactive. No replacement client DLL is
installed. The source archive and successful build log remain in
`out/online-setup/downloads` and `out/online-setup/server-build.log`.
