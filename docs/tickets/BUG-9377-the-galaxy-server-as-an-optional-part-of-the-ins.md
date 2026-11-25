---
id: BUG-9377
title: The galaxy server as an optional part of the installer
status: fixed
kind: feature
stage: Galaxy
reported: 2026-11-24
source: decision 8Y C (owner 2026-11-24)
role: host
versions:
fixed_in: 0.7.14.1-lineages71-dev
branch: feat/lineages-alpha
commits: b86c58af
tests: build/win_tests.exe --server-only (test_setup_server); by hand: the packaged setup, silent and its window, and the installed server with a QA game
signature:
docs: docs/12-server-and-players.md#server-in-the-installer
related: BUG-9376
---

## Report
Release 70-dev made the alpha's online worlds live, without the galaxy server program: a galaxy needs somebody who
runs it, and nobody but the developers had it. Decision 8Y (owner 2026-11-24: "8YC"): the server becomes an optional
part of the installer.

## Root cause
Not a bug: tools/package_duo.py never packed build/galaxy_server.exe and the setup had no optional parts.

## Fix
* The setup carries the server as a pack of its own (the update pack's format, tools/mkpack.py --server), Windows
  resource 102 beside the game's pack (101). It is never in the update pack: an in-game update of an older build
  installs every file of that pack, so every player would get the server.
* The setup's window has the box "Also install the galaxy server (to host online worlds for other players)", off by
  default, shown only when the setup carries the server, and ticked by itself when the chosen game folder has the
  server (so the setup that updates the game updates the server). Silent mode: `/server`; a silent run also updates
  a server that is installed. `/serverpack:<file>` (tests) takes the pack from a file.
* inst::do_install installs the game's files first and then the server's pack with upd::apply_pack, into
  SporebinEP1\mods\galaxy-server (the mods folder is the only place a pack may add files to). A server pack that
  names a file outside that folder is refused whole. A failure of the server's part says that the game's part is
  installed.
* The folder: galaxy_server.exe, Start-Galaxy-Server.cmd (the window that runs it), Read-me.txt and
  galaxy-settings.cmd (the host's name, worlds folder and options; installed only when missing). The worlds are kept
  in %APPDATA%\SporeCoop\galaxy-server\worlds. The port is fixed at 47900: the Co-op window knows a galaxy by it.
* The uninstall removes the program, the start file and the read-me; the settings file and the worlds stay.

## Verification
- `build/win_tests.exe --server-only` (TITAN, natively, 2026-11-24 19:14): ALL OK. It checks: no server folder
  without the option; the four files with it; the next version replaces the program and the start file and keeps the
  host's settings; a server pack naming SporebinEP1/mods/coop.dll is refused and the game's files stay; asking for
  the server in a setup without one fails with words; the uninstall keeps the settings file only.
- The packaged setup (out/dist/test-8yc, 12.8 MB, 1.3 MB more) on a dummy game folder: silent without `/server`:
  no server folder; silent with `/server`: the four files (CRLF text files); its window: the box off on a fresh
  folder (images/installer_galaxy_server_box.png), Install with the box left alone: no server folder, with the box
  ticked: the four files, opened again on that folder: the box starts ticked.
- The installed server started by Start-Galaxy-Server.cmd with a settings file of the test's (the name, a worlds
  folder, `--local`): "galaxy server 'Installer Test Galaxy' on UDP port 47900 (protocol 9)", bound to 127.0.0.1; a
  QA game (0.7.14.1-lineages70-dev) connected: `galaxy: connected ... galaxy='Installer Test Galaxy'`, encrypted, the
  server's line `player 1 ... connected`. No planet was made on it.
no-fail-run: a feature (nothing failed before; the checks above would fail without the option).

## History
- 2026-11-24: opened.
- 2026-11-24: built on feat/lineages-alpha (8Y C), not released yet.
- 2026-11-24: its update check added (BUG-9378): the server's pack also holds galaxy_update.exe and
  galaxy-server.version, and "the host runs the new setup after an update" no longer holds.
- 2026-11-24: released in 0.7.14.1-lineages71-dev.
