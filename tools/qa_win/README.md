# Windows QA and two-PC tests

Tools for testing on the owner's Windows PC (the QA copy of the game) and the MSI test PC over Tailscale. The Linux
lab (`tools/lab.py`, Wine lanes) stays the main test runner; these cover what needs a real Windows game or two PCs.
Background and pitfalls: docs/06-testing.md#testing-two-pc-msi.

| Tool | What it does |
|---|---|
| `qa.py <instance or msi> <command>` | One console command to a QA game on this PC, or to the MSI through the tunnel. Refuses a port that is not a QA game / the tunnel. |
| `launch.ps1 -Instance N -Profile P [-Resolution WxH]` | Starts a QA game (800x600 by default; 5 FPS from the QA copy's sporekit.ini). |
| `msi.ps1 ... -Instance 2` / `qa.py msi2 <command>` | A second test game on the MSI (its own profile, console 47801, tunnel 47851): a host and a guest on one PC. |
| `msi.ps1 deploy / start / stop / tunnel` | Copies the build to the MSI, starts or stops Spore there, opens the console tunnel. |
| `respawn_fade.py <dier> <watcher>...` | Cell: a respawn must not darken the other screens (BUG-9122). |
| `dis.py <instance or msi> dis <address> [bytes]` / `calls <crash base> <addr>...` | Game code read from a running game and disassembled (no Ghidra here); a crash's stack scan reduced to real call sites. |
| `hover.ps1 -Instance N -Pattern <regex> -Out <png> [-MatingCall] [-Up px]` | Hovers the REAL mouse pointer over a creature (its line in `coop.evolve mates`) and saves the window with the cursor drawn in (the console's `move` is a virtual cursor; the game's `shot` has no cursor). Owner's OK needed: it moves their mouse. |
| `social_hits.py msi <host> <npc>...` | Creature: befriending by real input on the MSI never hurts the creature on the host (BUG-9136). |

Rules (owner, 2026-11-11 / 2026-11-12):

* Never the owner's own install or profile: QA copies and test profiles only (`SporeKit-<profile>`); a save of theirs
  is copied into a test profile (read-only on their files).
* Console `click`, `drag`, `wheel`, `key` move the real mouse and keyboard: on this PC only with the owner's OK; on the
  MSI freely.
* Up to 3 test games on this PC at 5 FPS in small windows, unless a test needs a size (Adventure clicks: 1440x1080).
* A new build into a running game: copy it over the game's `mods/coop.dll`, then `mod.reload coop` only. Never
  `sym.reload` on the QA copy: it reads no symbol file there and leaves the game with none (`load symbols missing`,
  2026-11-12); restart that game.
* A header change needs no FORCE=1 any more (tools/build_windows.sh tracks includes since 2026-11-12), but the MSI
  and the QA copy always get the whole new build.

A typical two-PC session:

```
pwsh tools/qa_win/launch.ps1 -Instance 9 -Profile DuoQAJiffHost
pwsh tools/qa_win/msi.ps1 deploy
pwsh tools/qa_win/msi.ps1 start
pwsh tools/qa_win/msi.ps1 tunnel        # leave it running
py tools/qa_win/qa.py 9 coop.host 47839 Host-NoShare- 4
py tools/qa_win/qa.py msi coop.join <this PC's Tailscale address>:47839 MSI-NoShare-
```

Test players always use a name ending in `-NoShare-` (BUG-9222): their creations are never offered to
nor kept in anyone's Lineages Cache, so a QA game joining the owner's install leaves nothing in it. The tag is
hidden in the player list.
Since BUG-9331 (2026-11-21, owner: "All tests should all be -NoShare-") a test copy adds the tag by itself: any game
started with `-sporekit-profile:` (every launcher here passes it) or as a second instance hosts and joins with
"-NoShare-" added to its typed name. Keep typing the tag anyway. Only `coop.fix core.test_noshare off` (on both games)
lets two QA games share creations, for a deliberate Lineages Cache test.
