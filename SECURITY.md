# Security

## Reporting a vulnerability

Please do not report a security problem in a public issue. Use GitHub's private vulnerability report
(the repository's **Security** tab > **Report a vulnerability**), or, if that is not available, write to
the maintainer in the [SporeCoop Discord](https://discord.gg/7Yre7u4b8x) and ask for a private channel.
Say which version, what an attacker can do and how to reproduce it. A fix goes into the next release and
the report is answered there.

SporeCoop is a hobby project without a bounty. Supported: the newest release only.

## What the files are and what they do

Every release is built by GitHub Actions from the tag ([workflow](.github/workflows/build.yml)); no binary
is uploaded by hand. Source of each file:

| File | Where it goes | What it does | Source |
|---|---|---|---|
| `SporeCoopSetup.exe` | anywhere (it is not installed) | Finds the game, downloads the newest release from this repository's GitHub releases (or uses the `.pack` next to it), checks it against `SHA256SUMS.txt`, installs, updates and removes SporeCoop. Asks for administrator rights only when Windows refuses to write into the game folder. | [`installer/`](installer/), [`updater/`](updater/) |
| `steam_api.dll` (shipped as `steam_api.sporekit.dll`) | `SporebinEP1`; the game's own file is kept as `steam_api_orig.dll` | The loader: forwards every Steam API call to `steam_api_orig.dll`, then loads `sporekit.dll`. Without `sporekit.dll` the game starts unmodded. | [`loader/`](loader/) |
| `sporekit.dll` | `SporebinEP1` | SporeKit, the mod framework: finds game functions by name for the exact game build, hooks them (MinHook), the console, crash and hang logs. | [`sporekit/`](sporekit/), [`third_party/minhook/`](third_party/minhook/) |
| `mods\coop.dll` | `SporebinEP1\mods` | The co-op mod. Network: UDP to the players you host or join (port 47810), LAN discovery on the local network, one HTTPS request to `api.github.com` per start for the update check (`[update] check=0` in `mods\coop.ini` turns it off). | [`mods/coop/`](mods/coop/) |
| `mods\sporekit\symbols\*.sym`, `mods\*.ini`, `Data\SporeCoop.package` | game folder | Text files with function addresses, settings, the menu icon. | [`data/`](data/) |
| `install.bat`, `uninstall.bat` | `SporebinEP1` | The manual install and uninstall: plain batch files. | [`installer/`](installer/) |

SporeCoop reads and writes only the game folder, the game's own save folder (a guest's copy of the host's
world goes into a new save slot) and its logs (`SporebinEP1\mods\logs`). It collects no data; a report
(**Report a problem**) is a zip file on your disk that you send yourself.

## Antivirus warnings and verifying a release

Antivirus heuristics flag mods like this one (a replaced `steam_api.dll`, code hooks, downloads, unsigned
files). How to check that your files are exactly the released ones (`SHA256SUMS.txt`, signed build
provenance with `gh attestation verify <file> --repo WoodyWizard/SporeCoop`, the version information,
rebuilding from source) and how to install without the setup:
[docs/INSTALL.md, Antivirus warnings and verifying a release](docs/INSTALL.md#install-antivirus).

## Support

If SporeCoop is useful to you: [Buy Me a Coffee](https://buymeacoffee.com/chamomilewoods).
