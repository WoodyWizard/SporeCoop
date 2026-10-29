# 6. Testing

> Running the tests in parallel (lanes, `tools/lab.py`, warm resets, chaos bot, bad network):
> [10-dev-pipeline.md](10-dev-pipeline.md). This page covers the single-lane basics.

## 6.1 Environment

* The game is installed in the Bottles bottle **Games** (runner `caffe-10.0`, DXVK); the Spore registry entries are already there.
* Launch: `tools/spore.sh run [-p profile] [-i N]` → `bottles-cli run -b Games -e .../SporeApp.exe -- <args>`.
  Game arguments go after `--`. Bottles filters environment variables, so settings are passed
  as command-line switches (`-sporekit-profile:test`, `-sporekit-instance:2`).
* Build: llvm-mingw (`~/.local/opt/llvm-mingw-*/bin/i686-w64-mingw32-clang++`), `make`.

## 6.2 Profiles - tests don't touch your saves

`-sporekit-profile:<name>` redirects Spore's user data to
`AppData/Roaming/SporeKit-<name>/Spore`. `spore.sh run -p test` creates the profile from a copy of
your settings, but with a 1440×1080 window and no tutorials or hints. Instance `-i 2` uses the
`inst2` profile automatically. A fixture with a Creature stage save:

```bash
tools/spore.sh fixture save creature test       # capture
tools/spore.sh fixture restore creature inst2   # put into another profile
python3 tools/tests/load_game.py -i 2           # load it via the menu
```

Fixtures for other modes (`out/fixtures/*.tgz`) are made like this:

```bash
python3 tools/tests/new_stage_game.py <cell|creature|tribe|civ|space>  # new game via stage selection
python3 tools/tests/new_stage_game.py civ --npc-cities 3  # civ with cities of other nations
python3 tools/tests/space_setup.py      # space: new game, skip tutorial, leave the planet, save
tools/sporectl.py coop.save             # save (same as the menu button)
tools/spore.sh fixture save <name> test
tools/sporectl.py coop.load Game0 Planet   # load a profile save directly from the menu
```

`new_stage_game.py --name NAME` types the planet name in the last wizard step (`text` sends WM_CHAR,
so any letters: "Земля"). In the galaxy menu the discs of saved games are drawn with a dashed cyan
ring, free ones with a solid yellow ring; the slot windows of `coop.ui tree` do not tell them apart,
and their order changes after a planet card was opened (`--slot 0` is free in a fresh start with the
`creature` fixture). The creature wizard of GOG 2024 is the tribe one (a diet card opens the
Sporepedia: a creature, confirm, theme, difficulty, name). `SPORE_TUTORIALS=1 spore.sh run ...` starts a
test profile with tutorials and hints on, like a player's first game (default off).
`new_stage_game.py` wants a test profile without saves (empty `Games` folder) and the 1440x1080
window: it finds a free planet slot by itself (each slot ring of the galaxy menu has a 48x48 UI
window with id 0x02E5A4A8 in `coop.ui tree`; `--slot N` picks the N-th, `--slot X,Y` a point),
hovers before clicking and checks that "Choose A Stage" opened. The wizard after it clicks fixed
points; a run that ends with "FAILED: still GalaxyGameEntry" can simply be repeated.
The caption window "Choose A Stage" exists (and has its own visible bit) while the chooser is
closed; only its parents tell: closed `fl=0x2` at an off-screen rect, open `fl=0x3`. Until
2026-10-23 the check looked for the caption alone, so a first click that did nothing (a fresh
profile right after the galaxy intro) passed it and the wizard clicked into the void (two runs of
two failed); now every window of the `coop.ui find` chain must be visible.

The fixtures (tools/tests/fixtures.py lists planet and checked facts; tests take the planet from
there, never hard-code it: a fixture made anew gets a random planet name). All but `creature` were
made anew on 2026-10-23 (the old ones were lost with the game folder), on lanes 7/8 (GOG 2016 build):

| Fixture | Planet | What it has | Made by |
|---|---|---|---|
| `creature` | Game0/Scalinus | the host's red species at its nest (recovered from lane 1's test profile, so a few minutes played) | (old) |
| `cell` | Game0/Cadian | a new Cell game, ~330 cells in the pool, food 0, no weapon | `new_stage_game.py cell` |
| `tribe` | Game0/Zorispe | the player tribe (3 members) and one NPC tribe (it appears ~40 s after the intro caption, which is confirmed) | `new_stage_game.py tribe`, OK the caption, wait for `coop.tribe` to list 2 tribes |
| `civ` | Game0/Arigine | the player city only, intro text dismissed | `new_stage_game.py civ`, click 1410 1044 |
| `civ_npc` | Game0/Tavael | the player city (with its free vehicle) and three cities of other nations (nearest about 430 m away, the planet radius is about 500), intro text dismissed, $3000 | `new_stage_game.py civ --npc-cities 3` |
| `space` | Game0/Madas | in the home system (PlanetID 00460035), first mission ("Fly to planet Vulpica", P02460035, then scan the wreck there), $100 000, no interstellar drive | `space_setup.py` |
| `space_empire` | Game0/Madas | `space` + drive, $1 100 000, two colonies with spice, contact with two NPC empires (6.15) | `make_fixture.py space space_empire --cmd ...` (6.15) |

After each: `coop.save`, Enter on the "game saved" box, `spore.sh fixture save <name> test`. Both
players' creations (`creations-host.package` / `creations-guest.package`, the host's 11 creatures, the
guest's newest one is the Kirgle with the white bow of coop_guest_editor.py) were recovered on
2026-10-23 from lane 8's test profiles: they were byte copies made by `restore_creations` at the last
cold start (host and guest written in the same millisecond); the game later rewrites a loaded
package (same creations, ~150 bytes of the index differ), so other lanes had a different md5.

A new Civilization game has no other cities at first: they appear one by one when the player has
claimed spice and has money, after a timer (docs/re/civ.md "NPC cities"; about 18 minutes of play
in one test). `--npc-cities N` calls `cCitySimulator::AppearNextCity` N times instead.

Test runs don't get in the way of whoever is at the computer:
* **muted**: `prepare_profile` sets `AudioMuteAll true` and `AudioMasterVolume 0` in test
  profiles (the player's sound settings and PipeWire are left alone: PipeWire would remember
  the mute by application name and apply it to normal play too);
* **invisible**: `spore.sh run` starts the test games on a headless GNOME Shell
  (`tools/headless.sh up`: its own D-Bus session and runtime, config and state folders, a 5760x3240
  virtual monitor; its Xwayland display is passed to Bottles). Nothing appears on the user's
  desktop and the games keep drawing at 30 fps. SporeKit places each window in its own cell of a
  grid that fills the screen (`[sporekit] test_monitor = grid`: two 5760x3240 monitors, 8x3 cells, so
  every instance of lanes 1-8 has its own; a covered game window lags). Keep 5760x3240 per monitor:
  on 5760x4320 Wine offers no 1440x1080 and the games come up at 1920x1080 (fixed click coordinates
  then miss). `headless.sh` takes the display from its live Xwayland, never from a log: a stale
  display once made Bottles fall back to the user's desktop. The bus of the headless session must get
  its runtime folder too (env before dbus-run-session): its document portal otherwise mounts on the real
  /run/user/$UID/doc and unmounts it when stopped, and Bottles fails with "Can't find source path
  .../doc/by-app/com.usebottles.bottles" (fix: `systemctl --user restart xdg-document-portal`). Do not move test
  windows to a hidden workspace of the real desktop: GNOME sends them no frame callbacks, DXVK waits
  and the game stops at frame 1. `SPORE_HEADLESS=0` shows the games on the desktop (then SporeKit
  moves them to a non-primary monitor: `test_monitor = auto|primary|off`). `tools/headless.sh down`
  stops the headless shell. Sharing the real runtime folder with it would unmount the Flatpak
  document portal, so it never does.

* The test profiles' caches by key: a creation key a guest profile ever built (the host's "Judge"
  29020CC4, sent by `coop_creature_desync.py`) left its `creaturedata` in the guest's
  Pollination.package and a baked model in its graphics cache; the guest builds that body for the key
  whatever its own records hold (docs/re/creature.md 15). A test of "another creation under the same
  key" takes a key the guest never had (`coop_nest_species.py`: 290229B9).

## 6.3 Development loop

```bash
make && tools/spore.sh deploy
tools/spore.sh run -p test
python3 tools/tests/load_game.py                 # up to the Creature stage
make coop && tools/spore.sh deploy && tools/sporectl.py mod.reload coop
tools/sporectl.py shot                           # look at the screen
tools/sporectl.py exc                            # caught exceptions (address + symbol)
tools/sporectl.py hangdump                       # main thread stack to the log (hung before BUG-0050)
tools/sporectl.py stack.here 300 all            # main thread call chain, read ON the main thread
tools/sporectl.py stack.scan 300                 # exe return addresses on the main stack, no suspend
tools/spore.sh logs                              # tail of sporekit.log
```

New symbols in `data/symbols/*.sym` reach a running game only with `sym.reload` before the
`mod.reload coop` (mods resolve their symbols when they load; without it `H->sym` returns 0). A
`mod.reload coop` ends the co-op session (both sides show `offline`): to look at a two-player state
again, start the pair anew (`twoinst.start`, ~40 s). Cell stage: `coop.pool prey [r]` on the host
lists the NPCs within r m of a player with the game's own prey / threat answers for each player
(docs/re/cell.md 9), `coop.pool prey 30 sum` only the totals (play_cell.py samples them). `coop.pool realbite [r | c<key>]`
bites the nearest creature (or that one) through the game's Bite 0xE7B0A0 (DamageCell, or
BreakOrSwallow when lethal), as a bite event does; on a guest `coop.pool mirrorfix 0` turns the
2026-10-24 mirror fixes off (no facing correction, chunk and lethal bites stay local: the 0.7.2
guest) to show the difference in one session (`coop_cell_spin.py --without-fix` must fail).
`coop.pool` prints `cell_contacts: creature_pairs=… bite_events=…` (collision pairs of the own
cell with creatures, Bite calls of the own cell), on the host `own_bites` / `own_hits` /
`own_chunks`, on the guest `soft_turns`, `chunks_sent`, `chunks_taken`, `breaks_sent`.
Kills and meat (docs/re/cell.md 11, `coop_cell_meat.py`): `coop.pool kill [c<key>|-] [hit]` (one
bite through Bite, or DamageCell only; call it about once a second: a bitten cell is not hurt again
at once), `coop.pool meat` (all meat cells, `l…` = a guest's own), `coop.pool loot` (host: cells of
the last loot drops with age and position), `coop.pool eat <key>` (the game's EatFood on a cell: the
Karke fixture's cell is a herbivore, its mouth does not take meat), `coop.pool swimto <key> [tp]`;
counters `npc_deaths`, `loot_calls`, `loot_cells`, `kills_in`, `eats_late` (host), `mirror_kills`,
`kills_sent`, `death_replays`, `loot_skipped` (guest). Loot cells are not kept alive: teleporting the
players away removes them (eat right after a kill). `play_cell.py` run warm right after other cell tests can fail its "partner
cell is not replaced" check: `puppet_new` / revive counters are per game run, not reset by a warm
reset (seen 2026-10-24: 4-6 after coop_cell.py's knockouts and two warm rejoins, 0 when cold).

## 6.4 Test levels

| Level | What | How |
|---|---|---|
| Unit (Linux) | serialization, reliable delivery under 30% loss, localhost session, rejoin through a relay with the goodbye lost, a world-sized transfer over a netem-like awful link, interpolation, fakepeer interoperability | `make test` (`fakepeer_check.py` adds about 5 s) |
| In-game loopback | a puppet mirrors your own avatar | `sporectl coop.echo on 6 0.3` |
| Protocol | host in the game + a fake Python guest (GOG 2024 build) | `python3 -I tools/fakepeer.py 127.0.0.1:47810 --mode circle --build dc04aee5` |
| Integration | two game instances on one machine, host/join, world transfer | `python3 tools/tests/coop_two_instances.py` |
| Cell co-op | shared pool, knockouts and voluntary leave; sampled mirror cells disappear while the guest's avatar remains in the pool | `python3 tools/lab.py run cell_coop --retry 0` |
| Space trade | a tool gift by a real click on the F9 Space page (Antimatter Bomb: 5 of 12 charges, a new tool for the guest), no room (the guest at 97/99 keeps 2, 3 come back), a spice sale to the guest's home colony from the roster (both games' shared money +10 x price, the spice moves from the host's cargo to the guest's), faults; rivals mode reported as untested without space_rivalry; about 1 min after the 45 s cold start | `coop_space_trade.py` |
| Creature desync | the players' order of 2026-10-25 from the `creature` fixture, the guest without own creations: the host's pack gets a companion of a species only the host has (`coop.npc spawn 29020CC4` + `coop.npc recruit <id>`), both evolve through the vote (real "I'm ready", a body drag, "done"), both games are killed, the host loads its save, the guest joins again; after the editors and after the reload: the guest builds the companion, mirrors no NPC the host does not have, sees the host's kill, the host shows the guest's edited creature (fails on 0.7.2.2: companion "pending" (unknown species), 22 living copies after the editors) | `coop_creature_desync.py` (~2.5 min, cold start) |
| Nest species | the guest's My Creations get its Kirgle also under the key of the host's TigerHorse (290229B9; the guest restarted with that package), the host spawns an NPC of it (`coop.npc spawn`), sets its species record (`coop.social record`), saves and sends the world again: the guest builds the NPC from the host's records (`coop.creations bem <key>` on both, before and after the world went again), the host's record reaches the guest under the NPC's key and the host's (fails on 0.7.2.3: the guest's own body) | `coop_nest_species.py` (~4 min, cold start) |
| New game join | the host has an old game (fixture `creature`), starts a new one with a typed planet name ("Земля"; `--stage creature`, `--name`), hosts; the guest joins from its menu and must play the new planet (before the 0.7.2.1 fix: the guest got no game or an old one) | `coop_join_new_game.py` (~5 min, cold start) |
| Modes | any mode from a fixture: guest joins from the menu, receives and loads the world, both puppets, movement | `coop_mode.py --fixture <cell\|tribe\|civ\|space> --planet GameN/Planet`, `coop_mode.py --adventure` |
| Bad network | the guest leaves and joins again (even rounds lose the goodbye), timings of handshake, world transfer and reload | `SPORE_NET=awful python3 tools/tests/coop_rejoin_net.py --rounds 4` |
| Rejoins and loads | the guest leaves and joins again many times, the host world is loaded again through the galaxy menu, both fault logs (`exc`) checked after every round; `--reshare a:b` sends the world again during the load, `--race a:b` rejoins at random moments, `--menu a:b [--clicks]` joins from the galaxy menu, `--local-load a:b --cold` loads a save right after the menu came up in fresh games (the planet-texture fault of docs/re/space.md 8.3: 4 of 17 such loads faulted before the fix); `--stop` ends at the first fault | `python3 tools/tests/coop_rejoin_loop.py --rounds 30 [--reshare 0:2.5]` |
| Manual | real game over the network | two PCs / VPN, package from `tools/package.sh` |

`cell_coop` enables the voluntary-leave fix by default. Run with `SPORE_CELL_LEAVE_FIX=0`
to reproduce the old behavior using the same test; the mirror-removal check should fail. The
test restores the default afterward. Run `cell_meat` and `cell_spin` too when checking warm
resets after this test, since they start by leaving the previous session.
`fakepeer.py` defaults to protocol 7 and the GOG 2024 build fingerprint (`dc04aee5`). Use
`--build 922af7c7` for a GOG 2016 host, and `--protocol 6` for a protocol 6 host. The protocol
and build must each match the host. When the protocol changes, check the message formats before
updating the tool.
It returns an error if no welcome arrives before the run ends. A real host may save and send its
world on join; fakepeer does not load the world or implement the stage-specific messages. Its
movement uses a sphere and is not suitable for testing the Cell stage's two-dimensional pool.

## 6.5 Startup speed

The two-instance test is ready for checks in about 40 s (it used to take about 90):
* both instances start right away (2 s apart), without waiting for the first one;
* instead of long frame-count waits after loading, a readiness check plus a 4 s pause;
* the host's "game saved" box (the host saves before sending the world) is confirmed by the
  mod once it is visible (docs/08-ui.md); `twoinst` also presses Enter when the host's frames
  stall. A box left open pauses the host's game without stopping its frames: the host's cell
  does not move (coop_cell checks that no box is left);
* over LAN/localhost the world is sent with a large window (600 messages in flight at ping
  < 40 ms, 4 MB socket buffers): 3.8 MB in ~2.5 s instead of 7.5 s.

The game caps itself at 30 frames per second (33 ms per frame, `App::FrameLimit_ptr`).
`[sporekit] test_frame_ms` speeds up menu frames in test instances, `frame_ms` does so everywhere;
console: `frame.limit <ms>|auto`. Off by default: cell physics is tuned for 30 fps
(at 130 fps the player's cell drifts on its own), and UI tests wait in frames.

### Sync accuracy

`tools/tests/coop_sync.py` compares, in every phase, the position and rotation of each
synchronized object on the host and on the guest (command `coop.sync`: players, animals, cells,
citizens, vehicles, ships). It first waits until neither game's time is paused
(`cGameTimeManager +0x48` bit 0, read through the console): the GOG 2024 guest keeps its game time
paused for ~7 s after its world load in the tribe stage (black screen, no citizen `Update`, ~23 fps;
the 2016 guest is already running when `start()` returns; both builds pause ~5 s in the creature
stage). Each round reads the host twice 0.15 s apart, then the guest, then the host again; a guest
pose counts as right when the host had it within that 0.15 s window (`LAG_WINDOW`: the 0.10 s
interpolation delay plus one 20 Hz snapshot interval), i.e. its position is compared with the host's
path and its facing with the host's turn over the window. The guest shows remote objects that late
on purpose; a desync (wrong pose, stuck or drifting object) is far from the whole path and still
fails. The old measure (no window, lag included) is printed as `raw p90`.

Measured 2026-10-24 on the new fixtures (p90 windowed / raw), GOG 2024 lane 10 (3 runs) and GOG 2016
lane 3: cells 0.00 m; cell players 0.01 m, 0.3-0.9° (raw 0.22-0.30 m, 16.5-21.5°: the host's cell
turns every 4th round and the guest shows the turn ~0.1 s late, so one sample in eight sits at
~20°, enough for the old p90 of two players to fail on both builds); creature NPCs 0.3-0.6 m, players
0.4-0.9 m; tribe animals 0.4-0.8 m, citizens 0.4-0.6 m; civ vehicle 0.1-2.0 m, 0.9-5.8° (raw up to
2.0 m, 29°: the fixture has one vehicle, it turns in place when the test reverses it); ships
0.3-2.0 m (raw 1-11 m).

`tools/tests/coop_npc_multi.py` (4 players from two lanes, nobody moves, ~4 min) checks the NPC
sync of every guest over 40 s (p90 of the living NPCs <= 1.5 m; typical 0.25-0.45 m), that each
guest's avatar stands at the host's height for it, and that no game caught a fault. A guest whose
planet loaded without its height map (docs/re/space.md 8.2) fails the height check with about -6 m
and its NPCs at 6-9 m. `coop_multi.py` (chaos) samples one moment per minute, so a fight or a
rejoin can push a single round over its 3 m limit; read its per-round `n p90` lines.

`coop.npc herds` (creature stage) prints the planet radius (the avatar's |pos|), every herd made by
the mod (`ours=1`) and every game herd whose home is below 0.8 x that radius, with nest, enabled,
target, members, home and its |home|, territory radius and whether the herd's land point is
computed (`land_cached`); then the members of nestless herds (`puppet=0` in a herd of ours is a
puppet the mod lost). `coop_editor_strays.py` (two evolutions, ~6 min) reads it: homes of our herds
on the surface, no puppet left over (docs/re/creature.md 10). `coop.status` "strays: pending
removed" counts the left-over puppets removed after an editor.

`coop.npc untuned` lists the living creatures without archetype tuning (+0xE84) with species, herd,
the herd's archetype and nest, `ours` for puppets and proxies (`creatures [species]`: all of them);
`coop.npc archless` the herds without an archetype (ours); `coop.npc attackmsg <id>` (host) posts
the game's attack message for the host avatar and host NPC `id` (it reaches
CreatureStageEventHandler); `coop.npc strays off|on` switches the removal of saved strays
(docs/re/creature.md 12). `coop_creature_e84.py` (~4 min, cold): saves with the guest's puppet,
**restarts both games** (only then the puppet comes back from the save: a load in the same process
does not bring it back), loads that save, attacks the restored creature (the old code crashed at
0xD48634, 3 of 3 runs), switches the removal on, lets the guest join and rejoin. The games are
killed and started again inside the test, so `twoinst._monitors` are stopped and `twoinst.watch`
restarted around it. Screenshots and other files from a Bottles game cannot be written to /tmp
(the sandbox does not see it): use `twoinst.shot` (out/laneN/shots).

## 6.6 Known Wine quirks

* **Flatpak document portal.** Every Bottles launch bind-mounts `/run/user/<uid>/doc/by-app/com.usebottles.bottles`
  (FUSE of `xdg-document-portal`). It vanishes now and then while many Bottles sandboxes exit at once (seen every 10–20 min during lab runs on 2026-10-22; systemd then shows the portal restarted), and every start fails with
  `bwrap: Can't find source path …` in `out/run.<inst>.log`. `systemctl --user restart xdg-document-portal`
  brings it back; `tools/lab.py` checks it before every test and retries such starts (not counted as flaky).

* MinHook must not freeze threads (`VirtualProtect` may hang).
* In the second process of the same wineserver, blocking sockets/`select()` and starting new threads
  sometimes hang - the console runs on non-blocking sockets in a single thread.
* Spore stops rendering without focus; test runs enable SporeKit's "background mode".
* **A window on a hidden workspace is minimized.** Mutter marks every X11 window on a workspace that
  is not shown as iconic, and Wine then minimizes it (`WM_SYSCOMMAND SC_MINIMIZE`, `IsIconic` = 1).
  A minimized Spore draws nothing: `App::Canvas::Tick` (0x848260) calls the canvas's IsMinimized
  (vtable +0x5C, `IsIconic(GetAncestor(hwnd, 3))`) and sleeps 200 ms per tick, so a test game on
  the agent-ws `sporecoop` workspace stalled at frame 1 to 70 (watchdog "no frames", main thread in
  `Canvas::Tick`, 2026-10-22). Background mode ignores `SC_MINIMIZE` now (log line "minimize
  ignored"); hooking `IsIconic` alone was not enough (the main thread then spun in the message loop).
* **Exiting one instance kills all instances in the bottle** (that's how launching through
  bottles-cli behaves). So tests restart both instances together: the guest first, then the host.
* **Stack of a frozen game.** `stack` and `hangdump` suspend the main thread; under Wine that hung
  the console for good (2026-10-22). When frames stop but the console still answers `game.mode`,
  the main thread is pumping messages in some nested loop: `stack.here 300 all` runs on the main
  thread itself and lists every return address from its own frame up (modules and offsets; coop.dll
  offsets: `llvm-symbolizer --obj=build/coop.dll 0x1000_0000+offset`). `stack.scan` reads the stack
  from the TEB without suspending (stale values mixed in). Filter exe values to real return
  addresses (a call instruction before them) - UTF-16 text looks like code addresses. `winedbg`
  attach from `flatpak enter <wineserver pid>` failed with error 5.
* A hung Wine process can be left half-dead (a zombie leader with live threads)
  and keep holding UDP port 47810. `spore.sh kill` finishes such processes off with `SIGKILL`.

## 6.7 Reading console output in tests

* Tests parse `coop.status` with regular expressions that span lines, e.g. coop_cell / coop_creations
  / coop_vote expect the `puppet pos=… species=…` line **right after** the `remote <id> '<name>'`
  line. New per-remote lines go after the puppet line (the `together <id> <0|1>` line that
  `twoinst` waits for is the last line of each remote's block). On 2026-10-22 a `together` line
  inserted between them made coop_cell report "sync samples taken: samples 0" on every run while the
  partner cell was there and exact - it looked like a missing puppet / timing bug.
* `coop_cell.py` reads `coop.pool` keys with `re.search(key + '=(-?\d+)')` (no word boundary): a new
  field whose name ends in an old key (`own_food=` before `food=`) is read instead of it. On
  2026-10-23 that turned "shared food progress" into "0 -> 0"; the field is `own_points=` now.
* Cross-check before hunting a "missing puppet": the `remote … puppet=<ptr>` value, and in the cell
  stage `local_gameplay_cells` (counts every foreground cell except the own avatar) = mirrors + 1
  when the partner's cell exists.
* A real missing partner (`partner_dist=-1` on the guest, no puppet, bites and food still work)
  was the host's "game saved" box left open (fixed 2026-10-22): look at the host screenshot
  first. A host that "moves" 0.00 m in the sync samples is paused the same way.

## 6.8 Editors in tests

* `coop_vote.py` and `coop_guest_editor.py` open the creature editor through the evolution vote
  (`coop.vote editor`). In the 1440x1080 test window "done" is at `click 1416 1062`, cancel at
  `click 1376 1062`.
* The fixture's DNA is 0, so a test cannot add parts. An edit that costs nothing: drag a part that
  is already on the creature (`drag 1110 480 1060 420` moves the bow of the guest's Kirgle); the
  undo button lights up. An edited creature is saved to My Creations under a new key when the
  editor is left with done (one `Editor_Save` 0x577650 after the thumbnail state machine).
* When "done" seems to do nothing, probe the exit chain (docs/re/creations.md 7):
  `probe 0x5DF4B0` (button), `probe 0x57AAA0` (finish state), `probe 0x4F3C80` (the blocking error
  bit, first argument), `probe 0x58A350` (leaving). `coop.vote` prints `own editor` and how often
  the part-list error was waived.
* Leaving the editor after the host has evolved: the guest is in the stage for ~2 s, then goes to
  the galaxy menu and loads the host's world (~5 s); its own body is set 3 s after that.
* `coop_editor_desync.py` (~6 min, cold) gets into the editors the players' way, from the `cell`
  fixture with a guest **without own creations** (`twoinst.start(..., guest_creations=False)`):
  * Cell: `coop.pool mate button` shows the mating-call button (what collecting a part does), a click
    on it (721, 1018) calls the mate, `coop.pool mate` prints the mate, its distance and its screen
    point; clicking at it swims there and the editor opens on contact (both at the same level: food
    added by hand or a co-op respawn as a small cell can leave them at different levels, then nothing
    happens). Free cell edit: drag the second eye from (866, 540) to (980, 552) with `button` /
    `move` (a plain `drag` is too fast for the editors), then the species name field asks for a name
    on the first save (`click 720 1055`, `click 720 810`, `text <name>`), done (1416, 1058), cancel
    (1383, 1062).
  * End of the Cell stage: `coop.pool food 1000`, the cinematic (~20 s) ends with the caption
    "Congratulations! You're on the path to sentience!" (check 1410, 1048); the button under the
    portrait (1396, 1063, 'History' before) opens the evolution history, its check (1415, 1055) starts
    the Creature stage. The game is in Creature mode with the legless creature first; the first creature
    editor comes as a request inside the stage (the vote). Legs: hold the button on the first leg of the
    palette (275, 300) and move to (1000, 560) in steps; the editor mirrors it (65 of 115 DNA).
  * The old code failed it (2 of 2): the guest's mate vanished, the guest's game faulted at
    0xE83EC8 when the host's new cell arrived (and hung). The second vote after the host's mating call
    is a race (one run of two). It also checks that the guest's own creature editor shows its
    interface (`coop.ui tree 4` > 15 windows; a stuck one had 2-15), since a quick "I'm ready" right
    after the Creature world arrived left the guest in an editor without parts or buttons.
* `coop_cell_leave.py` (suite `cell_leave`, ~4 min cold) takes the same end-of-Cell path with the
  guest mirroring the host's pool (~380 mirrors), then the host closes the session (`coop.leave`
  on the host sends BYE: the guest runs the same disconnected callback as on a timeout; killing one
  game would kill both games of the lane's bottle). The old code kept ~80 mirrors in the Creature
  stage and faulted once at session end in `modes::cell_at` (coop.dll, caught by the SporeKit
  handler under Wine: `exc` shows `total faults: 1`, the game runs on; on Windows it was a crash).

## 6.9 Captions that wait for the player

* Some cinematics stop with a caption and a green check button and wait for OK as long as it
  takes, e.g. the Space death caption ("...you return to your homeworld") and the respawn caption
  ("We've built you a new ship..."). The ship respawns only after OK. Tests press it with
  `key enter` (works on an unfocused window in background mode); `coop.space hp` shows `cine=1`
  while the death caption is up and `movie=1` while any cinematic runs. A test that waits for a
  respawn without pressing OK waits forever (that was the "respawn never came" of
  `coop_space_play.py`, 2026-10-22; see docs/re/space.md 7.2.1).
* `movie=1` that never goes away (no HUD, Enter does nothing, clicks lost) right after a join was
  the galaxy menu's intro cinematic GGE_Intro carried into the stage by a load from the menu
  (docs/re/space.md 7.9, fixed in world.cpp). The mod ends it now; `coop.status` prints "cinematic
  <id> playing (menu intro)" while it runs and "menu intro ended N" after. The intro starts ~16 s
  after the menu appears and runs ~10 s, so a test that joins or loads in that window meets it:
  `twoinst.start(fixture, join_in_intro=True)` starts the guest once the host is in its world and
  joins at the start of the guest's intro (deterministic; `coop_space_guest_ui.py`, ~1 min to ready,
  1.5 min in all). A host that loaded before the intro gets it in Space ~17 s after its menu (ended
  by the mod after 1 s).
* `space` fixture, Vulpica (P02460035, the open first mission "scan the wreck"): landing there shows
  ~5 tutorial captions ("Something is showing up on the ship's radar...", `movie=1`, a next arrow
  at the bottom right) on each game, and while they are up the mouse wheel does nothing, so the
  ship cannot take off. `coop_space_puppet.py` failed rounds 2-4 that way whenever its random
  planet pick was Vulpica (lab 20261023-224651, lane 8: both ships stayed on the planet, the mod was
  fine). `Player.take_off()` now confirms captions (`captions()`, Enter) before zooming out, and
  the puppet test lands on a fixed planet per round (P01, Vulpica, P03, home).
* GOG 2024 lanes: for up to ~80 s after the load, zooming in at the home planet does not land
  (measured on lane 10: the first zoom that landed came 50 s after `start()` returned, also with the
  intro fix disabled; not investigated further). `coop_space_rescue.py` retries the first landing
  for 150 s. The 2024 lanes run at ~66-69 fps, the 2016 lanes at ~31 fps: `wait.frames N` waits half
  as long on 2024.
* The galaxy menu drops every click while its intro plays (~16-26 s after the menu came up) and
  for ~2-3 s after it (docs/re/space.md 7.9). On GOG 2024 `wait.frames 700` after the menu is ~10 s,
  so `coop_ui.py` clicked "Co-op" inside the intro and the host's window never opened (lab
  20261023-225702). The mod now skips the intro (Escape) when Co-op is clicked during it and opens
  the window 3 s after; `coop_ui.py` clicks the host's item while the intro plays on purpose (checks
  "clicked during the menu intro", "the menu intro was skipped") and the guest's 4 s after its intro.
  It types the lane's port before Host: the port field shows the lane install's saved `host_port`
  (mods/coop.ini, 47810 on lane 2 because the lane was copied from lane 1), and hosting failed with
  "bind() failed on port 47810" whenever lane 1 hosted at the same time (lab runs).
* Ships on a planet fly at the height of their own zoom (docs/re/space.md 7.10): two bots that landed
  with different numbers of zoom-ins are 100-450 m apart in height, and a partner far above is not on
  the screen at all. `coop.space dist` prints `repair <m>` (the ground distance) and `alt <m>`;
  `Player.match_height()` zooms toward it step by step (one notch is ~15-30 m on GOG 2024, ~45 m on
  GOG 2016) and `Player.rescue()` stops inside 40 m of ground distance. With the screen distance
  alone (< 25 px) the host flew past the disabled guest or orbited it 60-150 m away.
  `coop_space_rescue.py` zooms the guest out 6 notches in round 1 so the rescue is always tested at
  different heights (it failed every time with the old 3D repair distance: lane 1, GOG 2024).
* The player's ship in a star system can not be moved away from a test: writing its position or
  calling the locomotive SetPosition is undone at once (the ship stays near the camera target);
  to test "nobody near", `coop.space repairdist 0` sets the repair range to 0.

## 6.10 Playing bots

* `tools/tests/play_cell.py [--minutes 10] [--seed N]`: host and guest play the Cell stage through
  real input (hold the mouse and steer with `button x y down` / `move` / `button up`, clicks,
  W/A/S/D) and measure sync, shared food, knockouts, revives and damage; results and the bugs it
  found: docs/stability/cell.md. Its eyes: `coop.pool see` (kind, size ratio, distance, screen
  position of what is on screen); `coop.pool why` lists who removed mirrors on a guest.
* A cinematic in the cell stage sets `cine=1` in `coop.pool`: the level-up zoom ends by itself
  (~1-2 s), captions wait for the green check (bottom right) or Enter.
* A 10-minute session takes ~11 minutes with the start; the bot reaches 300-650 food (alone: ~270).

## 6.11 Bots that play through real input (play_creature.py)

* `tools/tests/play_creature.py --minutes 10 [--seed N]`: host and guest play the Creature stage in
  two threads (walk, WASD, right-drag to look around, hunt NPCs found with `coop.targets all`,
  joint hunts, social, eating, pause menu, F9 window with chat, zoom; the guest once exits to the
  main menu). Measurements and findings: docs/stability/creature.md.
* Clicking menus: `tools/tests/uifind.py` (`shown(con, caption)`, `press(con, caption, expect)`)
  finds a window by caption with `coop.ui find` and adds up the parents' rectangles (each is
  relative to its parent; flag 0x1 = visible). A menu stays "visible" under a dialog it opened:
  `press` waits for what the click should open, it does not wait for the button to go away.
* The F9 window: hold F9 for ~0.25 s (`key 0x78 down`, `up`): the mod polls the key state, a tap
  is missed. `coop.uistate` prints every widget with its rectangle. Typing into the chat line: click
  it, wait ~0.5 s (the edit takes the focus a frame or two later), then `text "..."`.
* Ability keys: the stance buttons above the ability bar are at `(W/2 - 30, H - 82)` (social) and
  `(W/2 + 43, H - 82)` (combat); keys 1..4 use the current stance's abilities (docs/re/creature.md 1).
* Player sync in play: standing players match within 1.5 m (p90 ~0.6 m), moving ones trail by the
  interpolation and walking delay (median ~0.5 s); in fights a partner's puppet snaps at 4 m.
  coop.sync's 1.5 m limit for players is for walking (coop_sync.py), not for fights.
* Social games of a guest (`coop_creature_social.py`): social stance, a click on the mirrored NPC
  (`coop.social` shows `engaged=<id>`), then hold the ability key (`key 0x31 down`, 0.3 s, `up`):
  the mod polls keys 1-4 and a bare `key 0x31` tap is missed, like F9. `coop.social act <id> [slot]`
  does the same without input; `coop.social species` prints the species table (both machines must
  print the same). A guest idling at a nest gets attacked: the test sets `coop.team hp 1000`.
* The host leaving to the main menu (`coop_exit_menu.py` part 3): "Exit to Main Menu" goes straight
  to the menu when nothing changed since the last save (the world was just saved for the guest), so
  the "Don't Save" dialogs may not come.
* A game that never leaves `LoadGame` (0x1654C08) keeps its frames running: the monitor and
  `Checks.faults()` see no freeze and no fault, and the watchdog writes no stack. Tests that load
  worlds check `game.mode` themselves (`coop_exit_menu.py` part 4 fails after 30 s in LoadGame; a
  normal load leaves it within about 1 s). The race of part 4 (the vote ends on the frame the host
  saves for the guest) is forced by polling the host's log for "back to the world" and sending
  `coop.vote go` right then.
* Several agents on one machine: each one gets its own lane; temp files belong in a folder of
  your own (subagents of one session share the scratchpad folder: another agent's `up.py` there
  started the cell fixture in this lane, 2026-10-23).

* Rivals mode (`coop_space_rivalry.py`, ~6 min on lane 6 after a 45 s start): both land on the
  fixture's home planet, the buttons of the F9 Space page are found by their caption in
  `coop.space rows` (row n = widget `sbtn<n>`) and clicked for real. Real PvP shots: the host flies
  to the guest (`Player.regroup`, < 80 m), palette "combat", key 1, clicks on the guest's point from
  `coop.targets all`; about 1 damage per hit, ~100 hits in 20 s. `coop.space rivalry` prints mode,
  own money, relations, every player's last budget and the hit counters; `coop.space rivalry shoot
  <id> <dmg>` is a hit of the own ship on a player's puppet through the hooked TakeDamage (the lethal
  hit of the test), `pol` the political ids of the puppets (FFFFFFFF = at war). Money is changed with
  `coop.res add money <n>` (local, as if earned; shared again in allies mode).

## 6.12 Space game bots (play_space.py)

* `coop.targets all` lists what a bot can fly to: in a solar system its planets (`obj P/C<PlanetID>`),
  on a planet its cities (`obj T<n>` ours, `X<n>` others), in the galaxy view the stars near the ship
  (`obj S/H<StarID>`), and partners off the screen as `far player <id> x y` (their projected point).
  `coop.space star` prints `active <PlanetID>`, the planet a click sent the ship to (a moon next to a
  planet often takes the click). Inputs per view: docs/re/space.md 9.2.
* The space fixture's ship has no interstellar drive: the galaxy view stays locked. Tests give it
  with `coop.space tool add interstellardrive` (a badge cinematic and a tutorial caption follow:
  confirm them with Enter). `space_empire` (6.15) has it, plus colonies, contacts and money.
* Just after landing, damage to the player's ship is ignored for a few seconds (`coop.space hit`
  and relayed NPC hits alike); wait or retry.
* `coop.space hp` prints `hp <n> max <n> ...` (no `hp=`): a parser that looks for `hp=` gets
  nothing, and `coop.space hit <-1 - x>` then heals the ship (negative damage).
* The tool palette's state (open or closed) is read from a screenshot (tab icons on row H-151 or
  H-51); a blind click on the wrong row fires the selected weapon into the world. Never press Esc
  twice: the second one opens the pause menu.
* The bots run in two threads with their own console connections; the measurement loop uses a
  third pair. Each run prints its seed; `--seed N` replays the same choices (the game itself is not
  deterministic, so the same seed gives a similar, not the same, session).

* Co-op contracts (`coop_space_contracts.py`): `coop.space contracts` prints the state as
  `key=value` (state, crew, wave, ships_left, ships_up, targets, money, tool, rewards ...);
  `contracts period 0` stops the automatic offers, `limit <s>` shortens the time limit of the next
  contracts (the timeout case), `offer armada here` (a raid on the colony the host is at or the
  colony of its star) / `offer survey <n>`, `ships` prints the pointers of the armada's raid ships
  that `coop.fleet list` prints next to the ids `coop.fleet shoot` takes, `notices` the
  last notices of the feature, `money` the empire's money, `tool <hex>` whether a tool is in the
  cargo. The rows of the F9 Space page are `coop.space rows`; row n is the widget `sbtn<n>` of
  `coop.uistate` (a real click: coop_ui.Ui.click). Landing shows tutorial captions and a defended
  raid shows a badge panel: both take the clicks of the co-op window, close the window and press
  Enter first (the test's `captions()`). One run takes ~6 minutes (cold start ~45 s, armada ~90 s,
  survey ~60 s, timeout 25 s), 2026-10-23 on lane 3.

### Shared diplomacy (coop_space_diplomacy.py)
* `coop.space diplomacy list` (the empires with a name, events or a state; `list all` every one,
  60 lines at most), `near [n]` (the NPC empires nearest to the home star: `home=<StarID>` to fly
  to), `table` (records with events: `rec <pol> flags=<1 war, 2 alliance, 16 met, 32 contacted> rel=..
  <event>=<value>...`; compare two games with it), `log`, `sync`, and the game's own functions:
  `apply <pol> [event] [scale]` (default the gift event), `war|ally|peace|break|contact <pol>`;
  `local ...` does the same without sharing (tests: make the copies differ).
* The test runs on `space_empire`: it checks the fixture's two contacts in both copies, then flies
  to the two nearest empires not met yet; the host's first-contact talk is `coop.space empire
  contact <StarID hex>` (spacecoop.cpp, the same calls as the game's handler, shared by the hooks).
* A cold start under load once left one caught fault on both games at 0x11ECE4F (a null object in
  code called from 0xA0F687, a logger-like routine, outside any diplomacy code); the next cold run
  had none. `ck.faults` counts it because `exc` counts since the process started.
* The test takes ~3.5 min after a cold start of the pair (~45 s): the jumps to two empires' home
  stars take ~5 s each, the rejoin ~15 s. It passed twice in a row on lane 4 (GOG 2016).
* Use an event that stays in the map to make copies differ: CommunicatedMean 0x5F62736 applied by
  hand changed the relation once and left no event, so the tables looked equal again.
* The first contact shows a tutorial caption on the player who made it ("Drake's equation was
  right... open your Communications Panel", with OK) and the caption's input lock drops the clicks
  on the co-op window ("timeout waiting for Space page", lab 20261023-225702 lane 9 and an earlier
  run on lane 2): the test confirms captions (Enter) before its clicks.
* An alliance needs a relation of at least 3: below it the game's own periodic check breaks it in
  either copy (docs/re/space.md 12.5); with one gift the relation was 2.44 and the alliance was gone
  ~1 min later ("after the rejoin the guest is still allied" failed on both builds). The test gives
  a gift worth 3 before the alliance.
* The `diplomacy list` output of all 2120 empires is cut by the console (~16 KB): the command
  shows only the interesting ones.

## 6.12.1 The galaxy map test (coop_space_map.py, ~3.5 min on a cold lane)

* `coop.space map roster [id]` prints the rosters (`own` / `shared` colonies), `map tags` the tags
  drawn in the last frame (box and the projected star point), `map project <star>` a star's pixel,
  `map planets [star]` the stars near the ship with their planet records (tech, spice, nodes),
  `map cost <star>` the game's flight price, `map go <id>` / `map fly <star>` what a click does.
* A colony for the guest: `coop.space tool add placecolony`, land on a planet of the home system
  (Vulpica in the `space` fixture), `coop.space map colonize` (the colony tool's OnHit at the ground
  under the ship, docs/re/space.md 13.3), then confirm the drop's caption with Enter: the colony is
  in the host's roster of the guest ~14 s later. The real aiming refuses every planet of the fixture
  but the home planet ("near spice").
* The tags vanish under the pause menu (Esc in the galaxy view opens it at once) and come back with
  a second Esc.
* Runs (2026-10-23): lane 2 (GOG 2016) passed 6 times in a row, the last with the final code (~3.5 min each). Lane 1 (GOG
  2024) once: rosters, the tags in the galaxy view, hiding under the pause menu and the rejoin
  passed; the guest's part failed: after the drive's badge a caption with letterbox bars stayed on
  the guest for over a minute through Enter (`a cinematic is playing`), so it never picked the
  planet. The test now also clicks the caption's check button (1410, 1048); not run on GOG 2024 since.
* Before the rejoin the host goes back to a star system: a guest that joins while the host is in the
  galaxy view is not "together" with it, `wait_guest_in` times out.

## 6.13 Playing the Tribe stage through real input (play_tribe.py)

Learned while writing the tribe game bot (2026-10-23, docs/stability/tribe.md):
* `drag` and `click` return at once; the game sees the press a few frames later and a drag takes
  ~15 frames: read the selection (`coop.tribe` `sel=1`) ~1.2 s after a `drag`, not right away.
* Selection boxes are screen rectangles around `coop.targets` points (the feet): a box from
  `y-55` to `y+25`, `x±35` catches a walking member; boxes that start on the HUD (the roster at
  the right edge, x > ~1375, the bottom bar y > ~950) do nothing.
* `coop.targets objs` lists everything on screen: `member <id> <owner>`, `obj t<id>` (citizens of
  every tribe), `n<id>` (animals), `tree` (fruit trees), `o2C9CC91` (small food plants),
  `hut<i>[p]` (huts, `p` = the player tribe), `tribe<i>[p]` (tribe centres, also off screen),
  `fruittree` / `tree` (planet trees with / without fruit left), `o<noun>` for the rest.
* **Hover before a right-click on an object**: the game picks the object under the cursor once per
  frame; `click` jumps the cursor and presses 3 frames later, which often still hits whatever was
  under the old cursor position (the ground: a walk order). `move x y`, ~0.4 s, then `click x y
  right` picked fruit trees 37 of 40 times (base point, 10 and 20 px above it); without the move
  about 1 in 3. `world_to_screen` also projects points hidden behind hills and the horizon: a
  bot filters targets by the distance column of `coop.targets objs` (metres from the planet point
  under the mouse: `move` the mouse to the middle first), e.g. < 90 m for trees around the village.
  Objects in front (the fire pit, the hut, members) take the click instead: skip targets within
  ~70 px of them.
* Camera: the arrow keys pan (~30 m/s), the wheel zooms, a right-drag rotates. The Home key does
  nothing in the tribe stage and **`H` opens the Spore Guide**, which swallows every later click
  (close it at `click 1338 59`). The minimap is the way to look somewhere: a click there centres
  the view on that spot. `coop.avatar` prints the planet point under the mouse (`pos=`), so 3
  clicks calibrate a linear minimap -> world map (~3.7 m per pixel on Cohrach); one correction
  step lands within 30 m.
* The tribe UI interaction mode: social at `click 725 1030`, attack at `click 795 1030`;
  `coop.status` prints `tribe ui mode=0|1`. The tribe palette at the bottom is locked in the
  fixture ("Conquer or ally with other tribes to progress"): nothing can be bought there.
* `coop.status` prints `tribe last order:` (guest: what it sent, host: what a guest order became
  and why it was rejected) and `tribe last local order: #n` (the host's own orders): a click that
  changes neither gave no order (it missed the target).
* `coop.tribe spawn <n>` (host) adds adults to the player tribe: test setup only.
* `coop.tribe select <id> 1` only sets the member's selected flag: a walk order (`coop.tribe
  walk`, a right-click on the ground) uses it, but a right-click on an object takes the terrain
  cursor's selection set, which only a real click / box fills: such clicks then give no order at
  all. Select by `drag` for real-input tests (play_tribe.py `Game.select_idle`).
* Agents running in parallel share one scratchpad folder: give helper scripts unique names (one
  agent's `reload.sh` was overwritten by another's, which then hot-reloaded the wrong lane).

## 6.14 Game bots in the Civilization stage (play_civ.py)

* Find things on screen with `coop.targets` (`city <host index> <owner> x y`, owner -1 = another
  nation, 253 = shared capital; `vehicle <net id> <owner> x y`), buttons with `coop.ui where <id>`
  (screen rect + enabled) or `coop.ui at <x> <y>` (every window under a point), building plots with
  `coop.civ slots <city>`, any object kind with `coop.civ nouns [noun]` (spice nodes: 403DF5C).
* `coop.targets` / world_to_screen do not know the planet: a point behind it still gets screen
  coordinates. play_civ.py projects with the `coop.cam` view rows and tests the ray against the
  planet sphere itself, and pans with W/A/S/D until the point is in a box (`Player.look_at`).
  Wait until the camera stops before reading screen positions (it glides ~0.5 s after a key).
* A box selection (`drag`) only works on what is on screen right now; a right click on water or
  across the sea gives a land vehicle no order (on the host too: compare host and guest rates, do
  not count it as a lost order). Attacks go through the target city's menu (docs/re/civ.md 9.1).
* A capture caption stops the host's world until Enter (`coop.civ` status `movie=1`).
* Console connections are per thread: a bot thread and the audit must not share a `Console`
  (replies get mixed: a `coop.uistate` answer parsed as `coop.civ`).
* `update_check.py` restores lane 6 to the original game (no loader); `spore.sh deploy` installs
  the loader again in such a lane.
* `update_check.py` (BUG-0211) runs `tools/package.sh` itself, installs that pack offline and offers
  the next version (COOP_VERSION, last part + 1): never a fixed version, it goes stale with the next
  release. One run per lane at a time (flock `SPORE-lab/.update_check-lane<N>.lock`, port 47999 + lane).

## 6.15 The `space_empire` fixture (Space features beyond the tutorial)

Made 2026-10-23 on lane 8 from `space` with one command (the planet and star ids belong to the
`space` fixture's galaxy; after a new `space` fixture pick new ones, see below):

```bash
SPORE_LANE=8 python3 tools/tests/make_fixture.py space space_empire \
  --cmd "coop.space tool add interstellardrive" --cmd "coop.space tool add interstellardrive_upgrade4" \
  --cmd "coop.space tool add energyupgrade5" --cmd "coop.space tool add placecolony" \
  --cmd "cheat moreMoney" \
  --cmd "coop.space empire colony 01460035 25" --cmd "coop.space empire colony 00460014 40" \
  --cmd "coop.space empire contact 0046102D" --cmd "coop.space empire contact 00460038" \
  --cmd "coop.space spice make 20" \
  --cmd "wait.frames 300" --cmd "key enter" --cmd "wait.frames 200" --cmd "key enter" --cmd "wait.frames 200"
```

What it has (checked by the facts in fixtures.py, `coop.space empire` prints it all):
* the ship in the home system (Madas, PlanetID 00460035), the interstellar drive (the galaxy view
  opens), drive upgrade 4, energy upgrade 5, the colony tool (one charge), $1 100 000;
* two colonies besides the home planet: P01460035 (home system) and Gabar P00460014 (the tutorial's
  beacon star, now an own star `H00460014` in `coop.targets all`); after a load they hold 10 and 5
  spice (the game clamps the stored spice at load: 5 per city, the value given is lost), the home
  planet 11; visiting P01460035 shows a generated colony (several cities, a spice geyser);
* first contact with the empires 80001FCC (home star 0046102D, 5.4 galaxy units away) and
  80001F53 (00460038, 5.9), the two nearest NPC empires; no comm screen was seen, no war, no
  alliance, no missions from them;
* 20 spice of the home planet's kind in the cargo (`coop.space spice`).

What it does not have, and why:
* **The tutorial is not done**: the mission is still "scan the wreck" on Vulpica (P02460035).
  The first zoom-out to the galaxy plays the tutorial caption about the beacon (Enter). While the
  mission chain is open, galaxy clicks on stars that are not own stars show a red "Distance: 1 (pc)"
  and the ship does not go there (also with drive upgrade 4); the beacon star and own stars work. ?
  whether this is the tutorial lock or the drive range.
* **Colonies only through `coop.space empire colony`**: with the colony tool the game refused every
  spot on Vulpica ("Colonies must be placed near spice."): unvisited planets have no spice nodes;
  planets far from home get a spice kind (record +0x198) only when generated.
* **Contacts only through `coop.space empire contact`**: first contact the game's way needs a flight
  into the empire's system and a talk on the comm screen; since fix/space-jump `coop.space jumpto
  <StarID>` does that flight (docs/re/space.md 7.3, `coop_space_jump.py` makes two contacts by clicks),
  but the fixture was made before.
* No raids, no missions of other empires, no trade route, no terraforming tools.

New ids for a new `space` fixture: planets of the home system with `coop.targets all` in the
system view (`obj P<PlanetID>`); a planet of another star needs a generated record (a star near
home that the tutorial points at, or one the ship visited); the nearest NPC empires: walk
`cStarManager` +0x150 (docs/re/space.md 9.6).

## 6.16 `coop_space_allies.py` (a partner's colonies as an allied empire)

* Fixture `space`; takes ~7 min on a lane (start 40 s). Runs 2026-10-23: lane 9 (GOG 2024) 6 passes
  in a row with the last code, lane 8 (GOG 2016) 3 passes. The host founds the colony itself with
  `coop.space allies colonize` (record level, a planet of the system it is in) in a free star near
  home (`allies free`; the nearest one, 4587540, is the tutorial's ruined colony: the test takes the
  second). The colony must be founded **after** the guest joined: the guest plays a copy of the
  host's save, so earlier colonies are its own already (shown as `skipped ...: your own star system`).
* `coop.space allies star <StarID>` prints the star record as the guest's game has it (empire id,
  owner name, tech level, each planet's tech level and cCivData pol / cities); `allies empire
  <pol>`, `allies cities` (with distance and screen point of each city), `allies raids` (target,
  state, raider ships), `allies raid <PlanetID> [n]` (the game's `raid -start` on a planet of my
  galaxy), `allies fight [dmg]` (the game's damage on the raiders of my raids on mirror colonies),
  `allies fake <name> <PlanetID>...` (a mirror without a partner), `allies remove|apply|drop`.
* Captions: the fixture's system starts with an intro (the camera orbits the star, cinematic
  manager busy) that only Esc ends; an Esc while landing left the planet view black (HUD only), so
  later captions get Enter only. The interstellar drive's caption needs Enter too.
* A hot reload of coop.dll (`lab.py reload`) while a mirror is applied leaves it in the game with
  the mod no longer knowing it (the next mirror skips that star: "a star system of another empire
  here"): `coop.space allies drop` first, or restart the games.
* The test steers over the city with `allies cities` (distance and screen point; `coop.targets
  all` did not list it in the test runs).
* The guest's trip goes into a system it never visited: there the planet view stayed black after
  the landing in every test run (HUD drawn, the cities without a position: `dist -nan`), the jump bug
  of docs/known-issues.md (fixed on fix/space-jump). The same trip into a system visited before
  shows the colony city (out/shots `allies_city`, manual run). The checks read the game data.

## 6.17 The galactic core (coop_space_finale.py)

* Uses the `space` fixture; both players get the interstellar drive with `coop.space tool add
  interstellardrive`. The host zooms out with the mouse wheel (`wheel 720 540 -5`, ~6 notches),
  `coop.space finale goto near|core` flies like "Fly to" does (galaxy view: the free instant travel
  of SpaceTeleportTo; a system: the Space page jump), the wheel in at the core enters its system,
  a click at (720, 480) flies the ship into the centre and the wheel in opens the core.
* Captions: confirm a caption only after it has been up for ~6 s, alternate Enter and a click on
  the green check (1410, 1048); the core cinematic needs an Enter about every 8 s (scrolling texts)
  and takes ~2 minutes. `coop.space finale` prints ctx / busy / cin (cinematic id, 2F7C7BDA =
  SPG_CenterOfGalaxy), zone (1 far, 2 near, 3 gate), entered / staff / charges.
* The core opens once per save, so every run needs a fresh fixture (a cold start restores it; a
  warm reset reloads the fixture too). A run takes ~8-10 minutes.
* `--alone`: the host presses "Enter now"; the guest is near the core but not at the gate and gets
  Steve's gift by message.
* The wheel in at the core takes ~30 notches: one zoom-out of 5 notches leaves the system, but the
  core's system opened after 9 to 11 wheels of 3 notches (measured 2026-10-28, lanes 4-7). A cap of 10
  failed 3 of 4 runs under load; the test wheels until the system opens (up to 40) and prints the
  counts (BUG-0213).

## 6.18 `coop_space_jump.py`: jumps into unvisited systems

`SPORE_LANE=N python3 tools/tests/coop_space_jump.py` (suite `space_jump`, ~5 min with the start, the
`space_empire` fixture). It picks unvisited stars near home with `coop.space empire near [n]` (the n
nearest NPC empire stars and free stars around the active star, with the contact flags, visited and
planet count, plus a `here` line for the current star) and plays: guest system -> NPC system (planets,
active planet, empire flag 0x20, first contact through the comm by clicks), host follows with the F9
Space page "Jump there", guest lands and jumps from the planet into an empty system, host zooms out
(wheel) and jumps from the galaxy view with the page button, guest jumps from the galaxy view into a
second NPC system (second contact). It checks the loaded planets (`coop.space planets`: all of them,
`coop.targets all` only shows those on the screen), the partner rows ("in another star system" /
"in your system"), the jump counters and no faults, and prints whether both games made the same
PlanetIDs for the new stars (docs/known-issues.md).
* Captions on the way that wait for OK (6.9): SPG_FirstContact_Intercept in the first NPC system (once
  per game), the tutorial's beacon caption on the host's own zoom-out to the galaxy (the jump's own
  galaxy pass skips it, docs/re/space.md 7.3); `settle()` presses Enter while `coop.space hp` says
  `movie=1`.
* The comm clicks (1440x1080): the comm tab above the empire's card at (288, 914), the first answer at
  (720, 725) three times, Goodbye at (424, 882); the contact flag (bit 1) is set at the end of the talk.
* The old jump stays reachable for comparisons: `coop.space jumpto <StarID> force old`.
* After several hot reloads (`lab.py reload`) the co-op session of the running games was gone ("offline",
  seen once 2026-10-23): restart the games for checks on both games.

## 6.18.1 The Steam build (manual, docs/05-re-notes.md 5.1.2)

Not part of the lab: the Steam exe starts only through the Steam client. Procedure (2026-10-24,
passed): install the mod into the Steam game's `SporebinEP1` by hand (INSTALL.md section 5), set
`background_mode=1` in its `mods\sporekit.ini`, `steam steam://rungameid/24720`, `agent-ws assign
<window of steam_app_24720> sporecoop`, a GOG 2024 lane host (lane 2: instance 3, co-op port 47812)
in the creature fixture, then on the Steam game (console instance 1) `cheat coop join
127.0.0.1:47812 Steamy`: the world loads, `coop.avatar` controls the creature, `coop.status` shows the
host's NPC proxies, `exc` 0 faults on both. Remove `background_mode=1` afterwards.

## 6.19 GPU load: a GPU reset took the whole desktop down (2026-10-24)

* With up to 18 test games at once (9 lanes; the GOG 2024 lanes draw ~66 fps, the GOG 2016 ones
  ~31), one game's DXVK submit thread hung the graphics ring of the RX 7900 at 00:18 on
  2026-10-24: kernel `amdgpu: ring gfx_0.0.0 timeout ... Process SporeApp.exe ... thread
  dxvk-submit`, `Illegal opcode in command stream`, the ring and MES resets failed, then a full
  `GPU mode1 reset` with `VRAM is lost`. Every GPU client aborted: the headless test display's
  Xwayland and gnome-shell, and also the user's own GNOME Shell (the session restarted, other
  apps crashed). The mod builds no GPU commands: it is a driver / DXVK fault under load, but the
  test load caused it.
* Rule: at most 6 lanes (12 games) at once on this machine (`tools/lab.py run ... --lanes 6`),
  fewer when the user works on the desktop. `journalctl -k | grep amdgpu` shows such resets;
  `coredumpctl list` the aborted programs.

## 6.20 Fullscreen keys (coop_alt_enter.py)

* The headless test display has two monitors, so Wine reports two D3D9 adapters ([0x1712438] = 2):
  the game's adapter switch (Alt+Shift+Enter) works there as on a two-monitor PC (docs/re/graphics.md).
* `key alt down`, `key enter`, `key alt up` is a real Alt+Enter: the game reads the modifiers with
  GetKeyState, which SporeKit's input hook answers while a key is held. Each toggle is one device
  Reset in the game log ("sporekit: device Reset -> 0x00000000"); fullscreen under the headless
  shell works and keeps 1440x1080. The test reads the renderer's fullscreen flag (IRenderer +0x6C)
  and ends windowed.
* `coop.avatar` prints `ground=` (cPlanetModel::GetHeightAt under the avatar) and `above=` (its
  height over the terrain) in the planet stages. A creature moved under the terrain (set_position 3 m
  down) is back on it within a frame: the game clamps it.

## 6.21 Deaths and respawns in the Creature stage (coop_creature_respawn.py, ~5 min cold)

* `coop.team die` kills the own creature through the game's damage path (TakeDamage, no filter);
  `coop.team hit <dmg>` hits it through the mod's damage filter, so the pack rescue applies (40 %
  health, once a minute per machine) and a second lethal hit kills. The death caption comes ~6 s
  after the hit; `uifind.shown(con, 'You were killed')` is the reliable "dead": after a kill next to
  the nest the health may climb again while the caption waits (mbDead stays 0), so `hp <= 0` is not.
  `key enter` presses OK; the respawn is a hatchling (`coop.avatar` `age=0`, half the health), the
  host's grows up 13-19 s later (its family greets it), the guest's 4 s of simulated time after the
  respawn cinematic (~14 s after the OK; docs/re/creature.md 13).
* Movement is checked with W held 2 s (`key w down` / `key w up`): 5-8 m when the creature has
  control, 0 m while it is a waiting hatchling. Right after the join the first W may do nothing
  (the test tries 3 times).
* ✗ `coop.npc hurt <player> <npc> <dmg>` (a host NPC "hits" a partner's puppet) dealt no damage in
  the game here, also 100000 and on the host's own creature: TakeDamage with that NPC as the
  attacker (55 m away, no fight) took nothing off. It still runs the mod's filter and the rescue,
  which is what `coop_team.py` checks ("guest survived a lethal hit"); the game's damage of that hit
  is not tested there.

## 6.22 Creature tests that stand around (coop_creature_kills.py, 2026-10-25)

* `coop_creature_kills.py` (~2 min after the start): the guest hunts a host NPC with real input
  (`coop.social goto <id>` puts the guest next to the mirror, the combat stance button, a click on
  the target from `coop.targets all`, key 1: ~11 bites for a 7.5 hp Kook), then checks the game's
  death flag of the guest's mirror (`coop.npc creatures <species>` " DEAD", not our "DEAD" of
  `coop.npc list`, which is only our bookkeeping) and the kill counts (`coop.social species`). Then
  the guest stands next to an NPC 45+ m from the host and the test reads that NPC's behaviour-tree
  stimulus slots with `mem.read` (docs/re/creature.md 14.2): works on old builds too (no new
  console command needed), which is how it failed before the fix.
* Long manual sessions: the creature's food runs out after ~10-15 minutes and it loses ~0.05 hp/s
  until "You starved to death! Remember to eat regularly!" (a caption that waits for OK, `key
  enter`); `coop.team hp N` refills the health, not the food. A test that stands still for minutes
  needs a fresh start.
* `tools/lab.py reload` (hot reload of coop.dll) ends the session: host and join again (`cheat coop
  host <port> Alice`, `cheat coop join 127.0.0.1:<port> Bob`; the join re-sends and reloads the
  world on the guest).
* Test helpers (host): `coop.npc epic <player> [dist] [archetype] [species]` (an epic near a
  player's creature; it does not attack the avatar, docs/re/creature.md 14.2), `coop.npc goherd
  <herd> [d]`, `gonpc <id> [d]`, `gopos x y z` (teleport the own avatar), `coop.npc archless all`
  (every herd with archetype and distance), `coop.npc partners on|off`.
* `call` with a `thiscall` that writes through a pointer argument: pass a real buffer; a null (or
  a short `s:` string) made `cTimeOfDay` presets fault and the game stopped drawing for good.

## 6.23 Migration to a new nest (coop_migration.py, ~5 min cold)

* `call Simulator::cCreatureGameData::AddEvolutionPoints cdecl f:250` on the host: the DNA sync
  gives the guest the same total (cPlayer +0x10F0), both play CRG_BrainUpgrade1 (a caption: Enter).
  After the evolution vote and both editors the host plays CRG_SpeciesMigrate; 1000 DNA jumps to
  CRG_BrainUpgrade4 and blocks the vote for a while.
* `coop.status` names the running cinematic by its FNV id (docs/re/creature.md 16 lists them); a
  caption waits for Enter. `call SporeCoop::Player_GetProperty thiscall <player> 0x514A219` is the
  migration state (2 = goal set), `call SporeCoop::CreatureGame_NestHerdFor cdecl 1` the new nest's
  herd, `call SporeCoop::Herd_NestPosition thiscall <herd>` its position;
  `coop.avatar tp x y z` puts the local creature there (walking 380 m is not needed for the claim).
* The world the host sends right after its editor has the guest's creature as a hatchling (no goal,
  no claim): the test saves again after the host's caption (`coop.save`, Enter on the box,
  `coop.share`). `--without-fix` (`coop.npc cinedefer off` on the guest) must fail: 5 faults at
  0xC6AE60 when the guest claims (4 of 4 runs on lane 6, 2026-10-25).
* Starting a cinematic by hand on a guest: `call Simulator::cCinematicManager::PlayCinematic
  thiscall <cm> s:CRG_ClaimNest 1 1 0 0 0` (cm from `call Simulator::cCinematicManager::Get
  cdecl`), ended with `call SporeCoop::cCinematicManager_EndCinematic thiscall <cm>`.

## 6.24 coop_creature_social.py is flaky in its ally phase (2026-10-25)

* Not in suite.py. Runs on 0.7.2.2, 0.7.2.3 and main (2026-10-25, lanes 2-4): 3 OK, 3 FAIL, on
  every version. The failing runs end the ally phase with the Kooks at (4, 2 impressed, 0): one of
  the three social games was refused by the game (`refused=2` in `coop.social`), so the third
  impression never came and "allies on both", "the ally reward was paid once" and the guest's
  ally card fail together. A test timing (the NPC was still busy or out of the social range),
  not a co-op regression. One run also failed "the pause menu is up: the partner's name tag is not
  drawn" once (not seen again).
* 2026-10-28 (fix/w4-tests): a refused game is now played again with the same Kook; still 1 OK of
  4 runs (out/lab/20261028-030521 OK, out/lab/20261028-033546 3 FAIL). In the failing runs a Kook
  died during the ally phase (the Kooks' table went to killed=1) and the species' impressed count
  stayed below the host's `impressed`. Still not in suite.py (BUG-0021).

## editor_strays fails under a full lab load (2026-10-25)
<a id="testing-editor-strays-load"></a>

* `coop_editor_strays.py` failed once in a 32-test run on 6 lanes ("host after evolution 2: no
  puppet left over", two creatures of the guest's species in one herd, puppet=0) and its retry too;
  alone it passed 3 of 3 right after on the same tree (lanes 2-4). Treat a single failure under load
  as flaky until it fails alone.

## Space: forcing freed ships and bad star ids (coop_space_stale.py, 2026-10-27, fix/w1-space)
<a id="testing-space-stale"></a>

* `coop_space_stale.py` (suite `space_stale`, warm, ~3.5 min with the 62 s rescue cooldown):
  BUG-0054, BUG-0092, BUG-0147. `--without-fix` switches the fixes off (`coop.fleet hitfix 0`,
  `coop.space starfix 0`, `coop.space repairfix 0`) and must FAIL; it is not in a suite.
* The one-frame window between the game freeing a ship and `squadron::tick` dropping it cannot be
  hit by timing. `coop.fleet freemirror <id>` (guest) frees the mirror and keeps the pointer listed
  until the next MSG_UFO_DAMAGE; `coop.fleet freepuppet <p>` (host) makes the freed puppet the
  next MSG_UFO_HIT's attacker. `att_freed` in `coop.status` counts attackers handed to TakeDamage
  that are not in the UFO list (where the game is called).
* `coop.space starrec <id>`: the record, the sector size and `read_outside` for one lookup.
## SporeKit freezes: kit_freeze.py (2026-10-28, fix/w1-sporekit)
<a id="testing-kit-freeze"></a>

* `tools/tests/kit_freeze.py` (suite `kit_freeze`, one game, ~4 min): six cases, each forcing the
  condition of a SporeKit freeze or crash with a `kit.test.*` command, then checking that the call
  answers, frames go on and no crash report appears. `--old` switches each case's fix off first
  (`kit.fix stackdump|cheatwait|reentry|hookkeep 0`): every case must FAIL then (the game is
  restarted after a frozen case). `--case stack,cheat` runs some.
* `stack` / `hangdump` (BUG-0050): `kit.test.stackheap` makes the main thread take the CRT heap
  and the process heap (`HeapLock`) and sleep 3 s, then the console thread dumps its stack. Before
  the fix the dump allocated while the main thread was suspended: both threads stopped for good.
  This is likely what "`stack` and `hangdump` hung the console under Wine" (2026-10-22, above) was.
* `cheat` (BUG-0065): `cheat sk shot` etc. go through the game's cheat manager, the same path as
  typing them into Ctrl+Shift+C.
* `logfault` / `excfault` (BUG-0115): a page fault while the thread holds the log's / the exception
  logger's lock; a test vectored handler (the last one) repairs the page after SporeKit's logger
  (the first one) saw the fault.
* `hookchurn` (BUG-0132): a hook on a function of sporekit.dll is made and removed 200 times while
  3 threads call it through a detour that spins before it calls the original.
* Runs (2026-10-27, lane 3): with `--old` all six cases FAIL (stack / hangdump / logfault / excfault:
  no answer and frames stop; cheat: `sk shot` 15 s, `sk wait.frames 2` no answer; hookchurn: a crash
  in the freed trampoline); with the fixes all PASS, the whole test 38 s. A first hookchurn made
  the hook while the threads ran and crashed with the fix too: that is BUG-0202, the gate in
  `hooks_churn_test` keeps it out of this test.
## Freed-pointer tests of the creature stage: host_away_kill, anim_teardown (2026-10-27, fix/w1-creature)
<a id="testing-creature-freed-pointers"></a>

* `coop_host_away_kill.py` (BUG-0082, ~50 s, cold start): the host `coop.menu`, the guest pushes its
  avatar into the kill list (`coop.npc killhold`) and reads `kill_held=` in `coop.npc`; the host
  loads back with `coop.load`. `--without-fix` = `coop.npc killfix off`: FAILS (kill_held 1..5).
* `coop_anim_teardown.py` (BUG-0091, ~130 s): the host sends MSG_ANIM 10x a second
  (`coop.overlay animsend <hex>`) while the guest does 6 rounds of `coop.menu` (11-12 s each);
  `coop.framefix` prints the animations and despawns that met a puppet the game no longer lists.
  `--without-fix` = `coop.framefix 0`: FAILS (15 made in 6 rounds). The host's ability keys
  (`key 0x31` ...) play no animation without a target, so they cannot drive this test.
* Both use a second Console connection to the host from a thread (the main thread talks to it too):
  one `Console` must not be shared between threads.
## Tribe and civ lifetime tests (2026-10-27, fix/w1-tribe-civ)
<a id="testing-tribe-civ-lifetime"></a>

* `coop_civ_reload.py` (suite `civ_reload`, civ_npc, ~1 min cold): alarm, host reload of the same
  planet from a fresh slot while connected, alarm, `coop.leave`; asserts `coop.civ tables`. The
  reload updates `lane_state` (slot) like a warm reset. `--nofix` (`coop.civ fix boost 0`, `fix
  world 0`) must FAIL.
* `coop_tribe_chief_removed.py` (suite `tribe_chief_removed`, tribe, ~1 min): `coop.tribe spawn 4`
  first (the fixture's guest has one adult only, so no second chief otherwise), `drop_chief`, then a
  forced reuse with `stale_chief`. `--nofix` (`coop.tribe fix chiefs 0`) must FAIL.
* `coop_tribe_gameover.py` (suite `tribe_gameover`, tribe, cold ~1.5 min): the caption comes some
  seconds after the last member is out: the test waits 15 s and finds it with `coop.ui find
  "Alas"`. The original fault did not come back on GOG 2024, also with `--nofix`: a regression test,
  not a proof of the fix.
## Core fix switches and the tests that need them (2026-10-28, fix/w1-core)
<a id="testing-core-fix-switches"></a>

* `coop.corefix [name 0|1]` lists and sets the core's fix switches; 0 restores the old behaviour so
  a test can show the bug (`--without-fix` of the tests below sets it): `vote_scope` (BUG-0043),
  `keepalive_ptr` (BUG-0107, used through `coop.exittest old`), `share_reset` (BUG-0140),
  `menu_drop` (BUG-0163), `update_pin` (BUG-0060; it applies to threads the current image starts).
* `make test` also runs the unit tests under AddressSanitizer (`make test-asan`, about 20 s more):
  a freed object used later fails there (BUG-0029). Leak detection is off.
* `coop_vote_stale.py` (two games, ~3 min): a vote from the previous session or stage must not
  run. `coop.vote action` starts an action vote like the superweapon's; `coop.vote` prints
  "actions run N, votes dropped N".
* `coop_leave_during_share.py` (two games, ~2 min): Leave 0.2 s after the share save was posted,
  then `coop.menu`; `coop.status` lists pending shares ("share for N saved, being written").
* `coop_exit_thread.py` (one game, ~1 min): `coop.exittest` ends the game with `ExitProcess(0)`
  without Shutdown; an abort on the way out writes "abort" into mods/logs/coop_exittest.txt. The
  game is gone afterwards (the lane restarts cold).
* `coop_ui_menu_leave.py` (one game, ~1.5 min): `coop.uistate` shows `in_menu=0|1`; the first answer
  after a load from the menu must say `menu_button=no`. Without the fix it said yes for one answer.
* `coop_reload_update.py` (one game, ~1.5 min): a local HTTP server on 47990 + lane holds the update
  check 8 s, `mod.reload coop` meanwhile. Under Wine the new shadow copy of coop.dll loads at the
  old base (the old one is unmapped), so an old thread does not fault: it runs on in the new
  image's identical code. The test therefore asserts the new image logs only its own result.

## Malformed messages from a joined player (coop_net_robust.py, 2026-10-27, fix/w1-net)
<a id="testing-net-robust"></a>

* `coop_net_robust.py` (suite `net_robust`, ~4 min cold) joins two scripted guests (the protocol
  of `tools/fakepeer.py`) to the host of a normal two-game session in the Creature stage. Guest A
  sends malformed and random messages of every type a guest sends; guest B checks what the host
  passes on. The host must keep running without a fault and the real guest must stay in the
  session.
* fakepeer packs every unacknowledged reliable message into one datagram: with more than a few
  dozen in flight the datagram is larger than the host reads and is never acknowledged (the first
  version of the test waited forever). Keep a scripted guest's reliable messages in flight small.
* Joining fake guests makes the host save and share its world (the modal "game saved" box stops
  the host's frames): the test presses Enter while the frames stand still, as `wait_guest_in`.
* The checks themselves (`mods/coop/net/validate.h`) are unit tested in `tests/validate_tests.cpp`
  (`make test`, built with ASan and UBSan).
* Seen on the way (lane 7, a 22-test run of 40 min): `coop_space_contracts.py` failed once with the
  known caught fault at 0x11ECE4F on both games after a cold start, and its retry once in the survey
  ("both at Kaver: surveyed (0/2)": the guest's page still listed Kaver while the host was at Rebel);
  two cold runs right after passed (out/lab/20261027-195928). Treat a single survey failure as flaky.

## Civ and tribe sync tests of wave 2 (2026-10-28, fix/w2-civ)
<a id="testing-civ-tribe-wave2"></a>

All take `--nofix` (the fix switches off) and must FAIL with it; logs of both runs in the tickets.
* `coop_civ_shared.py` (suite `civ_shared`, civ, ~1 min): two parts in one session. Crossing
  spends: the shared money set to 150, `coop.res add money -100` on both games in the same step
  (two threads, one Console each), the lowest money sampled for 2 s. Rejoin: guest `coop.role 1`,
  `coop.leave`, `coop.res add money 40000` offline, `lane.JOIN`; the host's money sampled for 12 s,
  then `coop.role` on both. Switches `coop.res fix reset|floor`, `coop.role fix`.
* `coop_civ_city_join.py` (suite `civ_city_join`, civ_npc, ~1 min): the guest rejoins; when its
  `coop.status` says "receiving world" the host calls `Simulator::cCivExpansion::SpawnNextCity`
  (instant 0, who 0: the hooked path that broadcasts). Cities compared by position (60 m).
* `coop_civ_ai_spawn.py` (suite `civ_ai_spawn`, civ_npc, ~1.5 min): two NPC cities founded on the
  host; each new city's tick buys a first vehicle at once, which is what the guest's own AI did
  too. Reads `coop.civ spawns` and `removed=` on the guest.
* `coop_tribe_ko.py` (suite `tribe_ko`, tribe, ~1.5 min): `coop.tribe pincer <npc> <own> 1000`,
  then `dead=` in the guest's `coop.tribe`. The member did not get up within 60 s in any run.
* Unit tests (`make test`): `test_lobby_roles`, `test_shared_ledger` in tests/net_tests.cpp; build
  them with `-DSPORECOOP_TEST_NOFIX=1` to see them fail on the old logic.
## Netcode wave 2: big messages, rejoin under a reused id (2026-10-28, fix/w2-net)
<a id="testing-net-w2"></a>

* Unit tests (`make test`): `test_big_reliable`, `test_state_after_leave` (three Sessions over
  localhost: the host queues two ~990-byte chunks for C, then B's last state and BYE arrive in one
  host update), `test_velocity_jitter`, `test_interp_latency_rise`, `test_alloc_id`.
  `build/host/net_tests --nofix` runs everything with the `net::g_fix` switches off (and the old
  inline id allocator): exactly these cases fail (12 checks), the older ones still pass.
* `coop_chat_long.py` (suite `chat_long`, warm, ~1 min): the guest sends a 1500-character
  `coop.say`, then a short one; the host's chat (coop.status `chat:` lines) must show both, the
  guest's `pending` stay low and its packets to the host stay ~33/s. `--without-fix`
  (`coop.corefix big_reliable 0` on the guest): neither line arrives, pending 8, 5280 packets/s.
* `coop_creations_rejoin.py` (suite `creations_rejoin`, cold, ~2 min): the guest plays its own
  creature, chooses "the world's own" (`coop.creations choose creature none`), leaves and joins
  again as id 1; the host's puppet of it (`species=` of the puppet) must be the world's species.
  After the rejoin the host shares its world again and the guest reloads it: the puppet first
  appears from the old world and is hidden during the reload, so the test waits 20 s and for
  `together` again before it reads the species. `--without-fix` (`creations_leave 0`,
  `id_reuse 0` on the host): the puppet is the old player's creature.
* Cell sync (fix/w2-cell): `coop_cell_sync.py` (suite `cell_sync`, ~1.5 min cold, two rejoins) and
  `coop_cell_net.py` (suite `cell_net`, runs over `SPORE_NET="delay=250,loss=0.05"`). Both take
  `SPORE_CELL_FIX=0`, which sets `coop.pool fix all 0` on both games (the old behaviour: the run
  that must fail). Pitfalls seen: after `coop.leave` the guest keeps its avatar, so a rejoin looks
  "in" (`wait_guest_in`) before the guest even started loading the host's world again: wait for
  `coop.avatar` to fail first. The avatar's `+0x358` is -1 (no level there): the level line is
  every 50 food points. Over the lossy link the guest's mirror map kept every local index for 20 s
  with the fix (157 rounds with a lost chunk).
## World sharing and updater files (2026-10-28, fix/w2-world)
<a id="testing-world-sharing"></a>

* `win_unit` (`tools/tests/win_unit.py`, ~10 s, no game): `make test-win` builds
  `build/win_tests.exe` (tests/win_tests.cpp + mods/coop/slotfs.cpp + the updater) and runs it
  under Wine in the lane's bottle (`bottles-cli run` of `cmd /c ... > file`: bottles-cli does not
  return the program's output); it works in `%TEMP%\sporecoop-win-tests` of the bottle. Real Win32
  file calls: where a received world goes and how it is written (BUG-0035, BUG-0087, BUG-0105),
  the updater's backups, pending list and duplicate paths (BUG-0114, BUG-0150, BUG-0089).
  `win_unit.py --old` switches every fix off and must FAIL. Wine enforces share modes on a file
  rename (a file opened without sharing cannot be moved: the pending test relies on it) but moves a
  folder that has a file open inside (Windows refuses), so the "file in use" case of a slot write
  only fails on Windows.
* `coop.worldfix [<name> 0|1 | all 0|1 | queue <id>... | autoload 0|1]`: the world sharing fix
  switches `busy_slot`, `slot_match`, `write_check`, `world_reset`, `share_drop_peer`,
  `share_one_save`, `load_in_editor`; `queue` queues shares with a save for those player ids in one
  frame (id 0 = the host itself, a stand-in for a second guest); `autoload` sets
  auto_load_world at runtime. `coop.status` prints "world saves posted N, for shares M, shares
  dropped, loads dropped, loads held" and whether a load is queued.
* `coop.corefix same_place 0|1` (BUG-0130); `coop.status` prints "host elsewhere=0|1, its world's
  messages dropped N, dna pending X".
* `coop_world_queue.py` (two games, ~2.5 min, `--without <names>|all`): one save for two shares,
  a share for a player who is gone, a load queued inside the editor, Leave while the host's world
  waits on the editor. The load-in-editor part without its fix loads from the editor and the
  guest logs ~170 caught faults: run `--without load_in_editor` alone.
* `coop_join_other_world.py` (two games, ~1.5 min, `--without-fix`): a second Creature world for
  the guest is the fixture slot copied with the planet renamed (`Otherworld.spo`): the game loads it
  under that name (another world id, same creatures). Only one fixture per stage exists otherwise.
* The test games' logs from a worktree: `twoinst.Monitor.game_log` derives the game folder from the
  repository root and points into the worktree's parent in a worktree; use the git common dir
  (`coop_rejoin_net.py`, `coop_world_queue.py`).
  Fixed 2026-10-28 (fix/w4-lab, BUG-0068): `lane.game_dir(n)` / `lane.game_log(inst)` give the
  lane's game folder and log from any checkout; twoinst, update_check, diag_check, kit_crash and
  kit_freeze use them.
## NPC ids, deferred puppets, the AI swap, creations after leaving (2026-10-28, fix/w2-creature)
<a id="testing-creature-npc-ids"></a>

* `coop_npc_reuse.py` (BUG-0044 / BUG-0023, ~1.5 min): the host kills an NPC (`coop.npc hosthit`),
  `coop.npc reuse <id>` makes the dead object alive again (a natural pool reuse could not be forced,
  docs/re/creature.md#creature-pool-ids); the guest's mirror of the host's id of that object must
  be alive and of its species in the guest's game (`coop.npc creatures`). Then `coop.npc nextid
  <id in use>` and `coop.npc epic 0 8`: no two NPCs with one id. `--without-fix` = `coop.npc
  reusefix off` + `idfix off`: FAILS (the mirror stays the corpse; id 1 twice).
* `coop_cinematic_puppet.py` (BUG-0066, ~1.5 min): the host plays CRG_ClaimNest (`call
  Simulator::cCinematicManager::PlayCinematic thiscall <cm> s:CRG_ClaimNest 1 1 0 0 0`, the caption
  keeps it running), the guest `coop.creations next creature` (the host builds a new puppet). The
  old puppet must have no host NPC id while it waits. `--without-fix` = `coop.npc deferpuppet off`:
  FAILS (id 23).
* `coop_npc_swap.py` (BUG-0110, ~1.5 min): the guest goes to NPCs far from the host (`coop.social
  goto`) until `ai_swaps` grows, `coop.npc swapfault` makes the next swapped tick skip its restore;
  `[cGameNounManager::Get() + 0x54]` must be the host's avatar again. `--without-fix` = `coop.npc
  swapguard off`: FAILS (the slot stays the puppet, ai_swaps stops).
* `coop_creation_leave.py` (BUG-0055, ~3 min, restarts the guest): the host chooses its TigerHorse,
  the guest gets the Kirgle under that key (`coop_nest_species.restart_guest`), joins and
  `coop.leave`s; `coop.creations bem 290229B9` on the guest must be the Kirgle's after leaving.
  `--without-fix` = `coop.creations worlddrop off`: FAILS (still the host's bem).
* `tests/npc_tests.cpp` (`make test`): the id allocator and the hit table, each with its old
  behaviour (`skip_used` false, `legacy`) checked to show the bug.
## Space wave 2 tests and their switches (2026-10-28, fix/w2-space)
<a id="testing-space-w2"></a>

Each runs on the Space fixture in ~1-3 min (warm) and has `--without-fix`, which must FAIL:

* `coop_space_fleet.py` (suite `space_fleet`): 25 x `coop.fleet spawn 2` on the host (about 20 stay
  listed: 2 chunks), `coop.fleet drop 0 5` on the host, the guest's game must keep the same ship
  objects (BUG-0037, switch `coop.fleet chunkfix 0`); a type 3 ship made on the guest
  (`coop.fleet spawn 3`, the posse) must stay (BUG-0084, `coop.fleet keepposse 0`). Type 2
  raiders fly off after ~30 s: keep the checks short after the spawn.
* `coop_space_trade_refund.py` (suite `space_trade_refund`): `coop.space trade hold 1` (the guest
  leaves gifts unanswered), `trade away 1` (the feature acts as in the galaxy menu: no inventory),
  `trade refundfix 0` (BUG-0058).
* `coop_space_allies_reload.py` (suite `space_allies_reload`, cold, ~5 min): `coop.menu` +
  `coop.load` of the fixture with an `allies fake` mirror; `allies reloadfix 0` (BUG-0038).
  `allies empire <pol>` prints the cEmpire pointer (`at=`), `allies nextpol [bump]` the id counter.
* `coop_space_diplo_mirror.py` (suite `space_diplo_mirror`): `allies detach <name>` turns a mirror
  into a plain empire of that galaxy (no contact, no alliance); `diplomacy mirrorfix 0` (BUG-0071).
* BUG-0141 (unique names) is a unit test in `make test`; `coop.corefix unique_names 0` switches it
  off in game.

## Space wave 3 tests and their switches (2026-10-28, fix/w3-space)
<a id="testing-space-w3"></a>

Each test takes `--without-fix` (switches the fixes off, must FAIL) and runs on the `space` fixture
(~3-6 min each on lane 6, GOG 2024):

* `coop_space_map_input.py` (`space_map_input`): `coop.space fpu` after 3 empire contacts
  (BUG-0126, switch `coop.space fpufix`), the jump notice (BUG-0072, `jumpfix`), `coop.space map go`
  while disabled (BUG-0063, `map teleportfix`; `coop.space disable 0|1`, `coop.space energy [v]`),
  the physical left button over a tag without a window message (`key 0x01 down/up`: `key 1` is the
  '1' key) and the message alone (`coop.space map postclick x y`) (BUG-0090, BUG-0145, `map clickfix`).
* `coop_space_seed_thread.py` (`space_seed_thread`): `coop.space namesheld <ms>` holds the name
  counter on another thread during the guest's generation (BUG-0123, `seedfix`). Stars from
  `empire near` with `visited=0 planets=0` only.
* `coop_space_defenders.py` (`space_defenders`): `coop.fleet spawn 6 own` on the guest,
  `coop.fleet impostor 1` / `puppet 1` on the host (BUG-0111, `coop.fleet typefix`). Do not give a
  ship an invalid model key in a test: every mirror of it costs a caught fault in
  cPlanetGfx_Create+0x8F0.
* `coop_space_armada_return.py` (`space_armada_return`): `coop.space takeoff` leaves the colony
  during the wave (the wheel did nothing then), `coop.space contracts raids` lists the raid
  missions on it (BUG-0046, `contracts wavefix`).
* Unit tests (`make test`, tests/space_tests.cpp): the jump target with 3 players (BUG-0072) and
  the aborted-raid set (BUG-0136, `coop.space allies abortedfix`).
## Wave 3 Cell, rescue and save tests (2026-10-28, fix/w3-cell)
<a id="testing-w3-cell"></a>

Each has an environment switch that turns its fix off; that run must FAIL:

* `coop_cell_guest_bite.py` (suite `cell_guest_bite`, Cell fixture, ~1 min warm,
  `SPORE_CELL_FIX=0`: `coop.pool fix guestbite 0`): the guest bites one NPC (`coop.pool kill c<key>
  hit`) after the host gives it hp 60 and then the species' "won't attack the player" flag or its own
  +0x111 (`coop.pool npcflag c<key> hp|spare|invuln`); the host's hp of it must not drop, as for the
  host's own bite. NPCs flee after a bite: both players `swimto <key> tp` before each one. BUG-0078.
* `coop_cell_rescue.py` (suite `cell_rescue`, ~1 min, `SPORE_CELL_FIX=0`: `fix rescue 0`): the guest
  knocked out (`coop.pool hurt 1 50` on the host), then `coop.pool selfhit 250` on the host: the
  game's death (`dying=1`), not a knockout. BUG-0131.
* `coop_space_helper.py` (suite `space_helper`, Space fixture, ~1 min, `SPORE_SPACE_FIX=0`:
  `coop.space helperfix 0`): the guest's ship disabled by `coop.space hit`, then a lethal hit on the
  host's ship: destroyed (state 2), not disabled. The frozen-partner part (a state over 3 s old)
  is not tested. BUG-0131.
* `coop_save_hygiene.py` (suite `save_hygiene`, cold, ~1.5 min, `SPORE_SAVE_FIX=0`:
  `coop.savestrip 0`): the host saves with the guest's puppet, both games restart, the host loads
  alone with `coop.npc strays off`: no creature without tuning, no herd without an archetype, no
  fault in 8 s (a null left in the behavior manager's list faulted at 0xB0EC8C every frame). BUG-0020.
* BUG-0160 is a unit test (`tests/cell_tests.cpp`, `numerics`): both the fixed and the old results.
## Netcode and the co-op window, wave 3 (2026-10-28, fix/w3-net)
<a id="testing-net-w3"></a>

* Unit tests in `make test` (each FAILS with `net_tests --nofix`): `test_lost_first_packet`
  (BUG-0030), `test_interp_wrap` (BUG-0053), `test_discovery_prune` (BUG-0039),
  `test_address_parse` (BUG-0138). `test_refpack_short` (BUG-0047) is a bounds check without a
  switch: the ASan build (`test-asan`) reports a heap-buffer-overflow with the old `dbpf.cpp`.
* In game, `coop.corefix` switches: `first_ack`, `time_wrap`, `lan_prune`, `addr_check` (the
  `net::g_fix` fields), `host_away_reset` (BUG-0085), `ui_status` (BUG-0104), `lan_pages` (BUG-0183).
  `first_ack` is read when a connection sends its first packet.
* `coop.lanfake <n> [seconds]`: n made-up games (`fake1`..`fakeN` at 192.0.2.x, TEST-NET-1, never
  joinable) answer the LAN discovery for that long; other lanes' hosts may be listed too, so a
  test looks for its own `fakeN` rows instead of counting rows.
* `coop_ui_status.py` (suite `ui_status`, cold, ~6 min): the guest leaves and joins again through
  the co-op window (a wrong port, then the address with a leading space), checks the status and
  the lobby world line once in, pages through six fake games with "More >", rejoins, and the host
  leaves while it is in the main menu (`coop.menu`): no "(host) is back" line in the guest's log
  after `disconnected:`, and the window status starts with "Disconnected". `--without-fix` sets
  the four switches to 0.
## SporeKit crash reports, text and the co-op window (2026-10-28, fix/w3-sys)
<a id="testing-sys-w3"></a>

* `kit_crash.py` (suite `kit_crash`, one game): `crash.test stackoverflow` (a thread recursing
  without end: a report naming 0xC00000FD, BUG-0056), `crash.test oom` (every report archive fails
  like an allocation in a full address space: the minidump and `last-problem.txt` are still
  written, the next start tells the player, BUG-0127), `prune` (12 planted old crash reports, dumps,
  reports and archives: only 5 / 5 / 5 / 3 stay, the file `last-problem.txt` names stays, BUG-0137).
  `--old` switches `kit.fix stackov / crashsafe / logprune` off: every case fails. Under Wine a
  stack overflow in a worker thread kills only that thread: the game goes on (the report is
  written anyway with the fix).
* `kit_freeze.py hookrace`: `kit.test.hookchurn 200 3 nogate` makes the hook while 3 threads run
  through the function (BUG-0202); `kit.fix hookpark 0` crashes the game (eip two bytes into the
  hooked function). The `hookchurn` case switches `hookpark` off itself: it tests MinHook's removal
  (BUG-0132).
* `win_unit.py`: `test_ini_unicode` writes "Вася 太郎" through the profile API into an ASCII
  coop.ini, reads a UTF-8 file edited by hand (BUG-0142); `--old` reads "????" / "JÃ¶rg".
* `make test` runs `tests/text_tests.cpp` (net strings cut between characters, name caps, overlay
  widths by letter cells, notification lines; `text_tests --old` fails, BUG-0129/0159/0164).
* `coop_window_layout.py` (suite `window_layout`, one game in the main menu): the resolution goes
  to 1280x720 through `SporeCoop::Renderer_SetVideoMode` (`call ... thiscall <renderer> <fs> w h
  -1`); the window opened again has every widget on the screen (BUG-0113); hosting, the overlay's
  status block (`coop.status`: `status block (x1 y1 x2 y2)`) is not drawn while the window is open
  and the first panel starts below it (BUG-0155). `coop.corefix window_recenter 0` /
  `status_block 0` (`--old`) fail. `coop.uistate` prints `screen W H`.
* New switches in `coop.corefix`: `text_utf8`, `notify_text` (the byte-based text code),
  `window_recenter`, `status_block`; in `kit.fix`: `stackov`, `crashsafe`, `logprune`, `iniutf`,
  `hookpark`.
## Tribe and Civ tests of wave 3 (2026-10-28, fix/w3-civ)
<a id="testing-civ-tribe-wave3"></a>

All take `--nofix` (the fix switches off) and must FAIL with it; FAIL and PASS runs are in the
tickets. Each needs ~1.5-3 min on a warm lane.

| Test | Fixture | What it checks |
|---|---|---|
| `coop_tribe_epic.py` (`tribe_epic`) | tribe | NPC tribe hit + one player: 30 food; one epic herd for three epics; no co-op epic after a real save; chiefs back to their size after both leave (BUG-0146, BUG-0121, BUG-0067) |
| `coop_civ_leave.py` (`civ_leave`) | civ | a leaver's vehicle is the host's, stays selected, drives (BUG-0031) |
| `coop_civ_sw_vote.py` (`civ_sw_vote`) | civ | a second superweapon request during the vote: the vote fires its own type, the sender is told (BUG-0117) |
| `coop_civ_caption.py` (`civ_caption`) | civ_npc | a capture caption nobody confirms ends after 8 s, the world moves (BUG-0187) |
| `coop_tribe_away.py`, `coop_civ_away.py` (`tribe_away`, `civ_away`) | tribe, civ | host in the main menu: the guest's member walks / vehicle drives (BUG-0151) |
| `coop_dna_away.py` (`dna_away`) | creature | DNA sent while the host has no creature and while it is away reaches the host's total (BUG-0151) |

* The host's chief size is still 1.3x while the host hosts alone after the guest left (its session
  goes on): `coop_tribe_epic.py` ends the host's session too before checking the host.
* A capture for tests: `coop.civ capture <city index>` (the game's Capture, plays the caption).
* `glyph_lint.py` (in `make test`): every non-ASCII character in a string literal of mods/coop must
  be in the overlay font (Arial / WGL4 subset, BUG-0161).

## Setup, install.bat and join by name (2026-10-28, fix/w4-installer)
<a id="testing-installer-w4"></a>

* `win_unit.py` (suite `win_unit`) now also tests the setup and `install.bat` under Wine, in
  `%TEMP%\sporecoop-win-tests` of the lane's bottle (never a real game folder):
  * `test_setup_packs`: `inst::local_pack` takes the newest `SporeCoop-*.pack`, `inst::do_install`
    refuses a pack older than the installed version (`Result::Older`) unless `downgrade` (BUG-0094).
  * `test_setup_close`: starts the real `build/SporeCoopSetup.exe` with a hidden window
    (`SW_HIDE`, found by class `SporeCoopSetup` and process id), clicks Install, sends `WM_CLOSE` and
    Close during the swap and checks that every file has the new version (BUG-0139).
    `SPORECOOP_SETUP_TEST_DELAY=<ms>` makes the setup wait between a file's two moves (and skips the
    "Spore is running" question); `/oldfix` on the setup's command line brings back the old behaviour.
  * `test_install_bat`: runs `installer/install.bat` twice in a fake `SporebinEP1`
    (`cmd /c install.bat < NUL`): a changed `mods\coop.ini` stays, a missing `.ini` comes back, the
    other files are replaced (BUG-0080). `--old` runs the install.bat of 7874654 (taken from
    `tools/package.sh` by `win_unit.py`).
  * Wine pitfall: `xcopy /exclude:<file>` fails and copies nothing when the exclude file does not exist.
  * Bottles (flatpak) does not see the scratch folder under `/tmp`: Wine test files go under the
    repository (`out/`, `build/`) or the bottle's `%TEMP%`.
* `make test-tsan` (in `make test`): `tests/release_tsan.cpp`, two writers and a reader on the
  setup's `upd::ReleaseBox` under ThreadSanitizer; `build/host/release_tsan --old` (no lock) reports
  a data race and exits 66 (BUG-0077).
* `coop_join_name.py` (suite `join_name`, creature, ~1.5 min warm): the guest rejoins by
  `localhost:<port>` with `coop.slowdns 4000` (every name lookup waits 4 s): the frames go on
  (`frame` from a second console connection) and the guest gets into the world; an unknown name
  ends with "cannot resolve". `--without-fix` (`coop.corefix async_resolve 0`) fails: 0 frames in
  2 s, the join command takes 4 s (BUG-0204). Unit test: `net_tests` `test_join_by_name` (fake
  `net::g_lookup` of 1.5 s).
## Tests on the game's state, flaky fixes, the extended suite (2026-10-28, fix/w4-tests)
<a id="testing-w4-tests"></a>

* `coop_creature_desync.py`: a kill by the host is checked on the game's death flag of the guest's
  mirror (`coop.npc creatures`), not on the " DEAD" of the guest's `coop.npc list` (the mod's
  `px.killed`). `--seed-nodeath` (`coop.npc deathfix off`: no `play_death` on a killed proxy) must
  FAIL (BUG-0148).
* `coop_team.py`: the joint hunt bonus is the NPC's health drop in the host's game (a lone hit d,
  the partner's hit right after 1.25 d) and the mirror's health on the guest; `--seed-nobonus` must
  FAIL (BUG-0135). `coop.npc list` prints hp with two decimals for it.
* `coop_social_console.py` (suite `social_console`): the cheat console opens by injected
  Ctrl+Shift+C only while the cheat manager's flag +0x64 is set, which is 0 on games that loaded
  their save by command (docs/re/creature.md#creature-cheat-console-flag): the test sets it first.
  `--without-fix` (`coop.social textfix off`) must FAIL (BUG-0149).
* `coop_creature_social.py`: a refused social game in the ally phase is played again with the same
  Kook; the test is still flaky (1 of 4) and stays out of suite.py (BUG-0021, see 6.24).
* `coop_civ_play.py`: the guest's "I'm ready" is clicked again (up to 3 times, 3 s apart) when the
  vote did not fire; the number of clicks is printed (BUG-0176).
* Suite `extended` (`lab.py run extended`, also nightly, not in full): hot reload
  (`hotreload_check.py`) and three players (`coop_multi.py --players 3`, `coop_npc_multi.py
  --players 3`). A third (and fourth) player runs in the test's own lane: instances 20+N and 30+N in
  lane N's bottle (`coop_multi.extra_instances`; console ports 47820..47839, co-op ports
  47830..47849, used by no lane). `SPORE_LANE_B=B` still takes another lane (its default was the
  next lane, which in the lab belongs to other work).
## Object census: coop.census and census.py (2026-10-28, fix/w4-census)
<a id="testing-census"></a>

* `coop.census` prints the shared objects of the current mode as the game has them, one line each:
  Creature stage: every live creature (`own=avatar|npc|proxy|puppet|local`, the net id of a host NPC
  or of a guest's proxy, species, dead flag, health, whether its own AI runs, position); Civilization:
  every vehicle (net id, owner, political id, position). Read-only. Format: mods/coop/census.h.
* `tools/tests/census.py`: `dump(con)`, `diff(host, guest)` (by net id: only on one side, dead on
  one side, another owner, more than N m apart) and `moved(a, b)` (objects of one machine that moved
  between two dumps: a frozen proxy shows as 0 m). `SPORE_LANE=N python3 tools/tests/census.py`
  prints the diff of a running lane.
* Not there yet (dev/census had them as a plan): Cell mirrors, tribe citizens, Space ships, the
  per-machine invariants ("inv" lines) and planted problems for a self-test.
* `coop_away_editor.py` (BUG-0205, creature fixture, ~5 min): uses `moved` as its oracle.
## One switch command for every fix: coop.fix (2026-10-28, fix/w4-registry)
<a id="testing-fix-registry"></a>

A bug fix's test must FAIL without the fix (CLAUDE.md 3). Every fix switch of coop.dll is in one
registry (`mods/coop/fixes.h`, BUG-0116):

* `coop.fix` or `coop.fix list`: `N fixes (M off)`, then one line per switch:
  `<area>.<switch> on|off BUG-NNNN  what the fix does`.
* `coop.fix <name> on|off` (also `1|0`; without a state it shows the one line), `coop.fix all
  on|off`. Off = the code before the fix. A switch is not saved: a restart has every fix on.
* A module registers its bool with one line next to its definition:
  `COOP_FIX("pool.purge", "BUG-0032", g_fix_purge, "cell: ...");`. The registry only points at the
  bools: the old per-module commands (`coop.pool fix`, `coop.npc strays|cinedefer|...`,
  `coop.civ fix`, `coop.tribe fix`, `coop.res fix`, `coop.worldfix`, `coop.corefix`,
  `coop.sessionfix`, `coop.framefix`, `coop.savestrip`, `coop.fleet|space ... <x>fix`, `coop.role
  fix`, `coop.give fix`) keep working on the same bools. The area of a name is the old command's.
* SporeKit's own fixes stay under `kit.fix` (sporekit.dll has its own table).
* `SPORE_FIX_OFF=<name>[,<name>...]` (tools/tests/twoinst.py): any two-game test switches these
  off on both games right after `twoinst.start()` (after the join: a fix that acts while the guest
  joins is still on then). Example: `SPORE_LANE=6 SPORE_FIX_OFF=npc.play_death python3
  tools/tests/coop_creature_kills.py` must FAIL.
* Switches added for 0.7.2.x fixes that had none: `game.adapter_key` (BUG-0012, Alt+Shift+Enter;
  `coop_alt_enter.py --without-fix` faults at 0x7777BE), `pool.forget` (BUG-0006),
  `npc.stale_proxy` and `npc.world_despawn` (BUG-0009), `npc.play_death` (BUG-0015),
  `creations.own_species` (BUG-0017). No switch (too large or no single branch): BUG-0002 (cell
  meat), BUG-0003 (join the played planet), BUG-0010 (species request).
* `tests/fixes_tests.cpp` (in `make test`): the registry and the command, and a scan of
  mods/coop: every `COOP_FIX` has a unique `<area>.<switch>` name and a BUG-NNNN ticket, and every
  bool named like a fix switch (`g_fix_x`, `g_x_fix`, `x_fix`, `g_fix`) defined `= true` has a
  `COOP_FIX`. A new fix bool without one fails `make test`.
* `coop_fix_registry.py` (suite `fix_registry`, one game, ~20 s): `coop.fix list` names every
  `COOP_FIX` of the sources, all on; `all off` / `all on` round-trips; the old commands see the
  same bools both ways; an unknown name is refused.

## editor_strays waits for the evolution cinematic (2026-10-28, fix/rel-strays)
<a id="testing-editor-strays-cinematic"></a>

* After an evolution the host's game plays CRG_SpeciesMigrate for about 10 s. The host sends its
  world only after it (BUG-0019) and the old puppet's removal waits for it too (BUG-0018, deferred
  destroy). A check of the world in that time sees the guest in its galaxy menu (`coop.npc herds`:
  avatar |pos|=0, no herds) and the old puppet still standing (BUG-0212). `coop_editor_strays.py`
  waits until the host's `for shares` counter went up, no cinematic plays, no deferred destroy is
  pending and the guest's log has a new `world: loading <planet> from` before it checks.
* `SPORE_FIX_OFF=core.strays,npc.strays` must FAIL (left-over puppets); lab run
  20261028-052840-fix-rel-strays did.
## Two flaky release tests: cell move direction, LAN pages (2026-10-28, fix/rel-flaky)
<a id="testing-cell-move-direction"></a>

* `cell` (coop_mode.py, cell fixture): the host's cell spawns at (100, 100) with an obstacle below
  it. Held towards 1000,760 (down-right on a 1440x1080 screen) it was pushed back and moved 0.1-0.6
  m in 2.5 s (lab 20261028-052417: 2 of 6 FAIL on "moved > 0.3"); towards 1250,540 or 1000,320 it
  swims about 6 m, towards 440,760 0.4 m. Screen up is +y, right is +x in cell coordinates. Away
  from the spawn the same hold moves the cell 5-8 m in any direction at ~68 fps. The cell test now
  holds towards 1250,540.
* The host of a fixture loaded straight from boot keeps the "galactic adventures" legal splash on
  screen in the Cell stage (seen for minutes, never on the guest, which joins from the menu); it
  does not take the mouse (a hold through it swims normally).
* `ui_status` part 3 (LAN pages): other lanes' hosts answer the discovery as well, each twice on
  the lab PC (wired and Wi-Fi address, BUG-0210), so with six lanes busy the list holds 18 games
  and the made-up ones are on pages 4-6. Part 3 starts while the guest still loads the host world
  (part 1's `wait_guest_in` returns before the transfer, the guest already plays a creature), so
  its leave sends the guest to the main menu, and the game drops the first click there (one lost
  click in each of 4 runs, lab 20261028-053914; on a guest that left 20 s before, every click
  turned the page). The test turns pages until every page was shown, and clicks "More >" again
  when the header did not change.

## creature: the guest walks with W, not with a click (2026-10-28, fix/rel-walk)
<a id="testing-creature-guest-walk"></a>

* `creature` (coop_two_instances.py --guest-menu) clicked at 300,700 to walk the guest. The guest
  spawns at the nest of the Scalinus fixture, at a slightly different place each run; from about
  (-168.8 -468.6 -84.5) that screen point is on the nest's rocks and twigs: the creature turns
  towards it (its rotation changes) and does not move, a second click neither (2 of 14 runs on
  lanes 2/3, lab 20261028-064630 #7 and #10; main 20261028-062902 2 of 3, 20261028-055128). From
  other spawn points the click walked only 1-4 m. W held for 2 s in the same stuck state walked
  8 m, so the guest's game was fine. The test now holds W for 2 s (5-8 m, lab
  20261028-065654 6/6); `--no-walk` sends no input and must FAIL ("walked 0.0 m").
* Clicks on the ground near a creature's nest are a bad walk input in general: the nest is an
  object, not walkable ground. Prefer W (camera forward, open grass at this fixture) or a point
  far up the screen.

## Core wave 5: votes, shared money at a join, host away, other worlds (2026-10-28, fix/w5-core)
<a id="testing-w5-core"></a>

New tests and test hooks (each FAILS with its `--nofix`, the fix switched off by `coop.fix`):

| Test | What | Hooks |
| --- | --- | --- |
| `coop_vote_drop_editor.py --case 1` (suite `vote_end_editor`) | a passed vote, the guest leaves while its own editor is due: its game stays in Creature (BUG-0234) | `coop.vote hold <s>`: a due own editor waits that long, so the Leave lands first (the editor otherwise opens 1-2 frames after the vote's end) |
| `coop_vote_drop_editor.py --case 2` (suite `vote_drop_editor`) | a cancelled vote (host to the galaxy menu) opens no guest editor (BUG-0234) | |
| `coop_civ_shared.py` step 3 (`--only-join` alone) | a guest's spend at another player's join counts once, both machines' game money (BUG-0220) | `coop.res joinsim add <name> <n>`: what a join runs on this machine, then the change, in one step |
| `coop_away_stages.py` (suite `away_stages`, Space fixture) | a host "editor" gives the Space guest no "you play on alone"; the menu still does (BUG-0233) | `coop.hoststate mode <hex> <s>`: our player states report that mode for a while |
| `coop_civ_other_world.py` (suite `civ_other_world`) | a guest in its own copy of the Civ planet (Otherworld) gets no host vehicle state applied (BUG-0231) | |

Unit tests: `npc_tests` test_share_wait (BUG-0232, `--nofix` = the global timer), `validate_tests`
test_dna_route and test_give_pick_leave (BUG-0233, BUG-0234; `validate_tests --nofix`), `net_tests`
test_shared_join_keep (BUG-0220).

Pitfalls seen:
* each case of `coop_vote_drop_editor.py` runs on fresh games: a rejoin between the cases loads a
  host save written while the guest's puppet existed, and the guest then faults every frame in
  `Herd_Update` (BUG-0256), which fails the run.
* `coop.vote go` while the guest is settled opens its own editor 1-2 frames after the vote's end
  message, so polling `coop.vote` for "own editor due 1" never sees it without `coop.vote hold`.
## Update leak and the updater's twin flags (2026-10-28, fix/w5-net)
<a id="testing-update-leak"></a>

* `tools/tests/coop_update_leak.py` (suite `update_leak`, one game, ~40 s, not exclusive: nothing is
  installed): a local server (port 47970 + lane) offers 99.0.0 whose pack calls itself the running
  version and holds one 20 MB file; 5 x `coop.update apply` stop at "the release pack is not
  newer". The SporeKit command `mem.commit` (VirtualQuery walk, any thread) gives the committed
  private memory; the `env` line under Wine is the working set and moves by tens of MB by itself.
  Seen: with the fix 231 -> 211 MB, with `SPORE_FIX_OFF=core.update_unwind` 233 -> 326 MB.
* `win_unit.py` passes `--old-<switch>` through to win_tests.exe, each turning one fix off while the
  others stay on: `--old-paths` (BUG-0242), `--old-lock` (BUG-0243), `--old-iniencode`,
  `--old-inilock` (BUG-0245). `--old` still turns everything off.
* `kit_crash.py` cases `thread_worker` and `thread_exit` (BUG-0248) compare the report's exception
  line with the fault logger's own `tid=`. `crash.test main` cannot make a main-thread crash report:
  the game's own handler swallows a fault in a console command on the main thread (no report, the
  game goes on); `crash.test faultthread` makes a repaired fault on a worker thread instead.
## Fix registry rules and the session-end structure (2026-10-28, fix/w5-tools)
<a id="testing-fix-rules"></a>

BUG-0214 (GitHub #148) completed the registry of BUG-0116 (see
[the registry](#testing-fix-registry)). The rules every switch follows (mods/coop/fixes.h):

* **One source.** In a coop.dll-only file the bool has `COOP_FIX(...)` on the next line. In a
  portable file (mods/coop/net/*, updater/*: also built into net_tests, fuzz_decoders, win_tests,
  release_tsan and the setup, without fixes.cpp) the bool is `FIX_SWITCH(var);` (fix_switch.h) and
  is registered once in `mods/coop/fix_table.cpp`; the members of `net::Fixes` too.
* **Names** `<area>.<switch>`, unique, one bool per name and one name per bool (`fix::reg` refuses a
  second name for a bool). Area: the switch it completes, else the file's area (coop.cpp `core`,
  world.cpp `world`, net/ `net`, updater/ `update`, cellsync.cpp `pool`, npc.cpp / game.cpp `npc`,
  squadron.cpp `fleet`, spacecoop.cpp `space`, civ.cpp `civ`, tribe.cpp `tribe`, shared.cpp `res`,
  creations.cpp `creations`, teamplay.cpp `team`, ...).
* **One bug per switch**: the off side is the code before that one fix. `tribe.chiefs` now gates
  only the chief and pincer tables; the epic's end has `tribe.epic_herd`. `upd::g_old_fix` became
  `update.old_suffix` (BUG-0114) and `update.keep_failed` (BUG-0150), `win_tests --old-suffix` /
  `--old-keep` show them apart (`--old` sets both).
* **`COOP_FIX_UNSAFE`**: the off side can corrupt memory or lose data (the crash tickets' switches
  `fleet.hit`, `space.star`, `core.frame`, `npc.kill`, `civ.boost`, `civ.world`, `tribe.chiefs`,
  `core.vote_scope`, `core.menu_drop`, `core.update_pin`, `core.keepalive_ptr`, `pool.cellat`, and
  `update.old_suffix`). `coop.fix all off` skips them ("N fixes off, M unsafe skipped"), `coop.fix
  all off --unsafe` does not, `coop.fix <name> off` always works; `coop.fix list` marks them.
* **Unit-test only**: `net.dbbf_bounds` (BUG-0208) and `net.refpack_bound` (BUG-0047) read received
  data out of bounds when off: no runtime switch, listed in fix_table.cpp's `kUnitOnly[]`.
  `net_tests --nofix` keeps them on (it runs to the end under ASan); `--old-dbbf` / `--old-refpack`
  switch one off and `make test-asan` checks that ASan stops them.
* **Not a lab switch**: setup-only bools (`g_pick_fix`, `g_release_lock_fix`, `g_release_merge_fix`)
  carry `// not a lab switch: <reason>`.
* No new per-module aliases: new switches are reached through `coop.fix` and `SPORE_FIX_OFF` only.
* `SPORE_FIX_OFF` is strict: a name the game does not know fails the test at once (before, the run
  went on with the fix still on); an unsafe one prints a warning. `coop.fix session_end` answers
  "unknown fix ... (did you mean session.end)".
* The scan (`fixes_tests`, make test) reads mods/coop, mods/coop/net and updater (.cpp and .h):
  every `FIX_SWITCH` and `net::Fixes` member is registered or in `kUnitOnly[]`; a bool `= true`
  whose line or comment line above names a BUG-NNNN or says `tests:`, `(tests)`, "old behaviour" or
  "before the fix" (at struct scope: not the BUG-NNNN) is registered or marked; an inline self-test
  on made-up sources keeps the rules from silently dropping out. `fixes_tests --names` prints the
  registered names.
* Tickets name their switches in the front-matter field `switch:` (registry names), checked by
  `tools/tickets.py check`.
* Hot reload sets every switch on again (the registry is per image).

**Session-end structure** (`end_session(EndPath, cell)` in coop.cpp, paths Leave, Unload, Restart,
Disconnect): block 1 the module calls (with `session.end` off only what each path called before
BUG-0033; Disconnect always calls all), block 2 the remotes (kept only on Restart with the switch
off), block 3 the core resets, always, each behind its own switch. A new reset of a coop.cpp global
goes into block 3 (mod state only: it may run inside packet delivery, BUG-0029; idempotent); a
module's reset into that module's `on_session_end()`.

## make test needs (2026-10-28, fix/w5-tools)
<a id="testing-make-test-needs"></a>

* No `$USER` / `$HOME` needed any more (containers, `env -i`): lane.py and spore.sh fall back to the
  password database (BUG-0252).
* A host compiler with sanitizer runtimes: a probe (`tools/sanitizer_probe.sh`, a stamp per
  `HOSTCXX` in build/host) builds and runs an ASan/UBSan and a TSan program first and says what is
  missing (clang: compiler-rt, Ubuntu `libclang-rt-<N>-dev`, or `HOSTCXX=g++`). Where TSan only runs
  without address randomisation (`vm.mmap_rnd_bits=32` with an older compiler) `release_tsan` runs
  under `setarch -R`.
* `SKIP_SANITIZERS=1 make test` skips them on purpose, with a warning at the start and the end;
  refused when `CI` is set.

## Lab: the free lane and interrupts (2026-10-28, fix/w5-tools)
<a id="testing-lab-free-lane"></a>

* A suite entry with `uses_free_lane=True` (today `update`) plays on `SPORE_FREE_LANE` and prints
  `LAB-WATCH lane=<N> since=<time>` once it holds that lane's lock; the lab then also counts crash /
  hang reports there (newer than `since`) and a game that dies there (BUG-0249). Its result line
  shows the lane as `lane 1 (+6)`.
* Lane 1's log folder is also the owner's install: a report whose `run:` line (crash reports
  since 0.7.3.x: `run: instance <N>, profile set|own, pid <pid>`) says instance 1 without a test
  profile is the owner's own Spore and does not count; without the line it counts.
* Ctrl+C or SIGTERM in any phase: the lab kills every test group it started, starts no new test or
  retry, ends the games of the run's lanes and of the free lane only when no other run holds its
  lock, and exits non-zero (BUG-0250). SIGHUP is left alone (nohup keeps working).
* `lab.py kill --lanes 3` is refused as ambiguous: `--lane 3`, `--upto 3` (lanes 2..3) or
  `--lanes 2,3` (BUG-0253).
