---
id: BUG-9378
title: The galaxy server checks for updates before it runs
status: fixed
kind: feature
stage: Galaxy
reported: 2026-11-24
source: owner 2026-11-24
role: host
versions:
fixed_in: 0.7.14.1-lineages71-dev
branch: feat/lineages-alpha
commits: f0a1d1b7
tests: build/win_tests.exe --server-only; by hand: galaxy_update.exe and Start-Galaxy-Server.cmd against a stand-in release on 127.0.0.1 (scratchpad make_web.py, run_start.ps1), and --check against GitHub
signature:
docs: docs/12-server-and-players.md#server-update-check
related: BUG-9377
---

## Report
Owner 2026-11-24, while 71-dev (the first build with the galaxy server in the installer, BUG-9377) was being
released: "Can we allow the Galaxy Server to check for updates before running?" Until then a host had to run the new
setup after every Lineages update, because the game's own update never touches the server.

## Root cause
Not a bug. Two things found on the way would have been bugs:
* Observed (code, updater/pack.cpp parse_release): every release asset whose name ends in ".pack" becomes the game's
  update pack, the last one wins, and parse_preview_releases drops a release whose pack is not exactly
  SporeCoop-<version>.pack. A server pack published as GalaxyServer-<version>.pack could have hidden the release from
  the in-game updater of every older build. The server's pack is therefore named *.srvpack.
* Observed (runtime): Windows refuses to start a 32-bit program named "...update..." that has no manifest without
  administrator rights (its installer detection; Git Bash: "Permission denied", exit 126). galaxy_update.exe has a
  manifest with asInvoker.

## Fix
* build/galaxy_update.exe (server/galaxy/update_main.cpp, built by tools/build_galaxy.sh from the game's updater,
  updater/): `--check` asks the Lineages releases for the newest build and compares it with the server's own version
  (galaxy-server.version, a new file of the server's pack): exit 10 when a newer one is out, else 0 (also when GitHub
  cannot be asked: the server then starts as it is). `--apply` downloads GalaxyServer-<version>.srvpack of that
  release, checks it against the release's SHA256SUMS.txt, parses it (its own hashes, the allowed paths), refuses a
  pack of another version or with a file outside SporebinEP1/mods/galaxy-server, and installs it with
  upd::apply_pack (the swap with a rollback, under the game folder's update lock).
* Start-Galaxy-Server.cmd runs the check first. A newer build: it asks "Update the galaxy server now", Y by itself
  after 30 seconds (at once when nobody can answer), N starts the server as it is. GALAXY_UPDATE in
  galaxy-settings.cmd: ask (default), auto, off. After an update the start file starts itself again (one line:
  the update replaces the file cmd is reading). Decision 8Z (A applied).
* The server's pack is a release asset now (GalaxyServer-<version>.srvpack, in SHA256SUMS.txt) besides riding in the
  setup. tools/package_duo.py writes it to out/dist.
* upd::cleanup_old also clears the server's folder of the backups an update leaves (*.sporecoop-old); the helper
  runs it at its start, the setup after it installed the server.
* The uninstall also removes galaxy_update.exe and galaxy-server.version.
* Makefile: the setup manifest's check no longer has double quotes inside single quotes (through mingw32-make on
  Windows that grep always failed, and a release build needed a second make run).

## Verification
All on TITAN, 2026-11-24 about 19:40, the server installed into a dummy game folder by the packaged 71-dev setup, a
stand-in release "0.7.14.1-lineages72-dev" served on 127.0.0.1:8971 (SPORECOOP_UPDATE_API):
- `--check`: "Lineages 0.7.14.1-lineages72-dev is out (this server: 0.7.14.1-lineages71-dev)", exit 10.
- `--apply`, the checksum file naming another hash: "the downloaded update does not match the release checksum.
  Nothing was changed.", exit 1, the version file still 71.
- `--apply`, a pack that also names SporebinEP1/mods/coop.dll: "names a file outside the server's folder", exit 1,
  coop.dll untouched.
- `--apply`, a good release: "the galaxy server is now 0.7.14.1-lineages72-dev", the read-me replaced, the host's
  galaxy-settings.cmd as the host wrote it, the helper replaced while it ran (its backup goes at the next start: 1
  left after the update, 0 after the next check); `--check` again: "up to date", exit 0.
- Start-Galaxy-Server.cmd: ask + N: the server runs as 71; ask + Y: update, the new start file, the server runs as
  72; ask with no input: Y at once; auto with a newer build: update without a question; auto when up to date and
  off: the server runs (off: no check line at all).
- Against GitHub (read only): a server of 72: "up to date"; a version file saying 69-dev: "Lineages
  0.7.14.1-lineages70-dev is out", exit 10.
- `build/win_tests.exe --server-only`: ALL OK (the new files installed, updated, removed; no backup left).
- Not run: a real download from GitHub (71-dev is the first release with a server pack, so there is none to fetch
  until 72-dev exists; the same download code runs in the game's updater).
no-fail-run: a feature.

## History
- 2026-11-24: opened.
- 2026-11-24: built on feat/lineages-alpha.
- 2026-11-24: released in 0.7.14.1-lineages71-dev.
