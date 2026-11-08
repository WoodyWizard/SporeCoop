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
Karke fixture's cell is a herbivore, its mouth does not take meat; `coop.pool diet` prints the carnivore /
herbivore flags of the own cell and the partners'), `coop.pool swimto <key> [tp]`;
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
| New players eat in a new Cell game | both profiles without saves or creations, the host starts a new Cell game through the stage wizard (`--diet herb|carn`: the leaf or the meat card, `new_stage_game.py --diet`), the guest joins from its menu (`--early`: during the stage intro); both play with the `play_cell.py` bots, then each swims to 6 cells of its diet (`coop.pool swimto`): its own game eats them (own points up) and the cell is gone (or shrunk) on both; `--apart N` lets the guest swim away from the host (food on its screen more than 50 m away, BUG-0174), `--far-probe N` then lets the far guest swim into food N times (its game eats it, the host applies the meal, the far food is the host's); suite `cell_far_guest`, `SPORE_FIX_OFF=cell.partner_tiles` fails | `coop_cell_new_game.py` (BUG-0279, ~8 min, cold start) |
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
  * After the host's editor the guest reloads the host's world (~4-10 s after it got the host's new
    cell) and its cell may hatch (`coop.uistate input_lock=1`, `coop.pool cine=1`, 5-10 s): the heart
    takes no click then and `coop.pool mate` stays `mate=00000000`. Part 4 waits for that reload and
    for the HUD before each press, and calls again when the mate is gone (BUG-0411; `--old-wait`, the
    old 5 s pause, fails about every second run).
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
* `kit_freeze.py hooknew` (suite `kit_hooknew`, BUG-0398): `kit.test.hooknew 200 3` hooks 200 functions
  never hooked before (copies in an executable page, each starting with a 5-byte jmp so MinHook's
  write cannot catch a thread between instructions, BUG-0202) while 3 threads call them; every round
  has its own original pointer and detour thunk, the detour counts runs with a null original (the
  real detour calls address 0). `SPORE_FIX_OFF=hookorig` (kit_freeze reads it for SporeKit switches,
  as `--old` does for all) fails: ~34000 such runs in 200 of 200 rounds; with the fix 0 (about 30 s
  with the game start). In game logs the fault is `exception ... at 0x00000000` with a sporekit.dll /
  mod return address as the first stack word (docs/02-modkit.md#modkit-hook-original).
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

## Wave 5 world tests (2026-10-28, fix/w5-world)
<a id="testing-w5-world"></a>

* `tribe_editor_world` (BUG-0217): the host opens an editor in Tribe through `coop.vote editor`
  (works in Tribe now: a member's species) and leaves with the red X (1380, 1062), since Done is
  blocked for the tribe species. Checks the host's game: no co-op epic after the share save and
  after the session. `SPORE_FIX_OFF=tribe.editor_world` fails.
* `civ_null_local` (BUG-0223): `coop.civ nulllocal <frames>` forces Civ's/Tribe's local entity
  null (the real trigger of the old world change). Checks the guest's vehicle objects and its
  order on the host. `SPORE_FIX_OFF=civ.world_epoch` fails.
* `civ_caption` step 0 (BUG-0224): a camera cinematic started with PlayCinematic must still play
  after 13 s. `SPORE_FIX_OFF=civ.caption_only` fails.
* `save_hygiene_leave` (BUG-0255): `coop_save_hygiene.py --leave`, the partner leaves before the
  save. `SPORE_FIX_OFF=savestrip.empty_herds` fails.
* `win_unit` (BUG-0254): `test_slot_fallback` (`slotfs::g_test_held` makes a slot unmovable under
  Wine the way an open file does on Windows) and `test_slot_leftovers`; `--old` fails.
* Pitfall: a warm reuse keeps `coop.fix` switches of the run before (a `--nofix` run directly on a
  lane, then a warm run: still off). The lab restarts a lane cold after a failure.
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
## Space test hooks of wave 5 (2026-10-28, fix/w5-space)
<a id="testing-space-away"></a>

* `coop.space away 1|0`: the Space modules act as outside Space (no cargo hold) while the Space
  world stays, as in an editor or an adventure entered from Space. Cleared at session end; a test
  that leaves it on breaks the later Space tests of the lane until the session ends. Used by
  `space_down_away` (BUG-0228/0229) and `space_gift_refund` (BUG-0226). `coop.space trade away`
  is trade's own older hook.
* `coop.space downstate` (disabled state, world epoch, owed clear), `coop.space gift` and
  `gift hold|noroom 0|1` (receiver side), `coop.fleet impostor_id|impostor_mission <p>` and
  `coop.fleet ident` (BUG-0218), `coop.space contracts tough_reuse|hideships 0|1` and
  `contracts wave` (BUG-0227), `coop.space allies samegalaxy` (BUG-0225).
* The owed spice, the owed invincibility clear and the disabled state are memory only: a hot
  reload of coop.dll drops them.
* `space_down_away` part (b) loads the fixture again from the galaxy menu (about 40 s).
## Cell wave 5 tests and the puppet move check (2026-10-28, fix/w5-cell)
<a id="testing-mode-puppet-pos"></a>

* `cell` / `space` (coop_mode.py, BUG-0251): the move check asks the guest's game where the host's
  cell / UFO is (`puppet pos=` after the host's `remote 0` line of coop.status) and polls up to 8 s
  after the move: within 1.5 m (Cell) / 8 m (Space) of the host, nearer than to the old place, moved
  by half the host's move. Seen on lane 2: Cell 0.3 m off after 6.4 m, Space 0.5 m off after 4 m.
  `--net-pos`: the old check (the network sample); `--hold`: seeded failure through `coop.puppet
  hold on|off` (the guest's remote puppets are not driven): FAILS with the new check, passes
  `--net-pos`. Tribe / Civ print a "cursor marker (network)" line: there the puppet is a mod marker
  at the network sample.
* `cell_ko_gap` (BUG-0238, ~1.5 min warm): `coop.pool kodrop <s>` on the host holds back the
  guest's knockout refreshes while its position states flow. The guest must be 30 m away and stay
  knocked out in its own game (`downed=1`) or the host revives it during the gap and the test
  proves nothing; the host is held with `coop.pool goto 0 0`. `SPORE_FIX_OFF=pool.ko_grace` FAILS.
* `cell_food_away` (BUG-0239, ~1 min warm): `coop.pool away 1|0` on the host. `SPORE_FIX_OFF=
  pool.food_ack_away` FAILS (guest 10 above, then a dip).
* `cell_guest_bite` (BUG-0078, BUG-0240): also bites a non-solid cell (rock first: a plant is often
  eaten while the test runs) and meat, each given hp 60, with `coop.pool kill <key|-> anyhit
  [kind]`. `SPORE_FIX_OFF=pool.guestbite_type` FAILS.
* `cell_respawn` / `cell_respawn_swap` (BUG-0221, ~1.5 min warm): the game's death in a session and
  what follows, in both roles; prints `coop.cell pool` before and after
  (docs/re/cell.md#cell-rebuilds-in-session). A guard: nothing failed when it was written.
* `cell_editor_invuln` (BUG-0237, ~3 min, cold: `guest_creations=False`): invulnerable into the
  own cell editor, a host bite hurts 4 s after the return. A guard (passes on 0.7.3.0).
* `make test`'s net_tests "LAN game list: pruned and capped" (BUG-0039) failed once here while
  this lane's games ran (`d.stored() == 0` after 6 s): real discovery answers reach the test's
  socket. It passed on the next run.
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

## The co-op campaign: a whole game of four bot players (2026-10-29, dev/campaign)
<a id="testing-campaign"></a>

`tools/tests/coop_campaign.py` (BUG-0152) plays one co-op game with four players from the Cell
stage to the galactic core, the way ordinary players play it, and writes down everything that goes
wrong. Suite `campaign` (in no other suite): `tools/lab.py run campaign --lanes 1 --skip 1,2,3,4,5,6,8,9,10`
(lane 7 only: `run` has no `--lane`, it is read as `--lanes`, BUG-0259) (the whole game,
~2.5 h) or `campaign_short` (Cell, the first stage change, Creature: ~25 min).

* **Players**: Alice (host) and Bob are the lane's two games, Carol and Dave are instances 20+N and
  30+N in the same bottle (`coop_multi.start_extra`, profiles `mp1` / `mp2`). Four games of one lane,
  nothing of other lanes.
* **Phases** (`campaign_lib.plan`): play Cell, advance, play Creature, advance, play Tribe, advance,
  play Civ, advance, play Space, the finale. `--from <stage>` resumes at a stage from its fixture
  (`campaign_lib.FIXTURE`: cell, creature, tribe, civ_npc, space), `--to <stage>` stops after one,
  `--minutes` is the play time per stage, `--advance-minutes` the time a stage change may take on the
  host, `--no-finale`, `--no-jump`, `--no-rejoins`, `--noise 0.06`, `--seed N` (printed; the game is
  not deterministic, a seed replays the bots' choices only).
* **Play**: every player runs the stage's own bot (play_cell / play_creature / play_tribe /
  play_civ / play_space Player classes, one thread each; the tribe Game steps its four players in
  turn) wrapped in `Hands`: between bot steps it answers captions (green check after 4-7 s), votes
  (F9 and a real click on "I'm ready"), editors ("done"), and with `--noise` chance does what players
  do besides: idles 10-45 s, opens and closes the co-op window, the pause menu (not in Space), the
  host saves. Once per stage a random guest leaves and joins again.
* **Checks** every 30 s on every machine: dead console, a new crash / report file of the four
  instances (by file time: the name has Wine's local time, which was an hour off), Monitor freezes,
  `exc` fault count up, the guests' stage (`game.mode`) and `together` on the host (not in Space:
  `together` there means the same star system, and players fly apart on purpose), per object kind
  the 90th percentile position error against the host (twice `coop_sync.LIMITS`, coverage >= 85%,
  `coop_multi.measure`), shared food / money (`coop.res`), the game's objects in Creature and Civ
  (`coop.census`, `campaign_lib.census_problems`: host NPCs within 90 m of the guest's avatar that the
  guest lacks, proxies the host lacks, alive/dead, owner, > 10 m apart). Stage, together, sync,
  resources and census count only when seen in two checks in a row (`Persist`): a rejoin or a world
  load is no desync. During rejoins and stage changes only the health checks run.
  A guest whose pause menu is open ("Resume Game" shown) is not compared for sync: its game runs no
  simulation and its cell mirrors move only by the 3 m jumps (BUG-0289, 2026-10-30).
* **Findings**: `out/lane<N>/campaign/<run>/findings.md` and `findings.jsonl`, one per key (repeats are
  counted), with the time, stage/phase, players and evidence: the last 30 log lines of each game
  involved and a screenshot of each (`<nn>_<kind>_<player>.log.txt`, `campaign_<nn>_<kind>_<player>.png`).
  Kinds: dead, crash, freeze, fault, stage, apart, desync, census, shared, stuck, info. Any kind but
  `info` fails the run; so does a run that did not reach `--to`. `summary.txt` has the timeline.
* **Shortcuts** (every stage change and co-op mechanic itself runs for real; listed in the
  docstring and printed in the timeline as `shortcut:`): Cell end `coop.pool food 1000`; Creature end
  `AddEvolutionPoints 3000` (never call it outside the Creature stage: in the Tribe stage it crashed
  the game in `AddEvolutionPoints+0x97`); Tribe `coop.tribe spawn` for more raiders; Civ
  `cCivExpansion::SpawnNextCity` for the cities still to come and `cCity::Capture` for the player's
  political id; Space the interstellar drive by `coop.space tool add` and `coop.space finale goto
  near|core` for the host's trip to the core. A stage change that does not finish in
  `--advance-minutes` (default per stage, `ADVANCE_MIN`) becomes a finding (`stuck`, for the Tribe
  stage `info`: its end by raids is not sure to come in time) and the host **jumps**: it loads the next stage's fixture with
  `coop.load` in game and the guests follow through the world share (coop_stage_switch.py's path).
* **The Creature -> Tribe change the players' way** (found while building it, lane 7): with the bar
  full the History button (1400, 1062) shows "Speed Demon" and its check (1415, 1055) starts the
  evolution vote ("Alice wants to evolve (Creature)"); after the vote the host's creature editor
  opens ("done" 1416, 1062); then the fire cinematic 2677CD86 plays and stops at the caption
  "Congratulations! You are about to enter the Tribal stage." (check 1410, 1048). The host sends its
  world to the guests during it ("a cinematic plays for 120 s, the world is sent anyway") and again
  in the Tribe stage, so each guest loads twice. The tribe intro cinematic 856F3820 ends with the
  caption "With a roof over your head ..." and ~50 s later "A rival tribe of ... has formed" (both
  wait for the check). `coop.status` `movie=1` (in the civ line) is up for any cinematic or caption
  in every stage: the campaign's caption detector.
* **The later stage ends the players' way** (debug runs on lane 7, 2026-10-29):
  * Tribe: raids by real right-click orders (play_tribe, plan `raid`) with the shortcut members do
    destroy an NPC village (badge cinematic), but new tribes form after each (1 -> 4 other tribes in
    two minutes); with the help of `coop.tribe pincer <victim> <attacker> 60` on the other tribes'
    members two villages fell in 30 minutes (run 20261029-011243) and the stage did not end, so the
    campaign gives it 20 minutes (`ADVANCE_MIN`) and then jumps to the civ_npc fixture (an `info`
    finding). An automated Tribe end needs a faster conquest or the alliance path (open).
  * Civ: `SpawnNextCity` gave 6 more cities (9 of other nations on civ_npc), `cCity::Capture(city,
    playerPol, 0, 0, 0, 0)` takes one per call (its capture caption, the city then belongs to a guest,
    owner=1 in `coop.civ`); a captured city may ask "choose the city's specialty" (Economic and
    Military, sometimes Religious too: a modal the campaign answers with the Military button found by
    caption with uifind inside the dialog's area; the HUD has a "Military" tab label too). A nation
    can also open its talk screen ("We can see the writing on the wall ...") and wait for the answer
    "We happily accept your surrender." (found by caption): History does nothing while it is up.
    With all cities taken the goal "Capture All Cities" is checked; History and its check start the
    vote ("Alice wants to edit a design (Civilization)"), then the spaceship editor opens **empty**:
    "done" says "You need at least 3 parts to Save" (three parts dragged slowly from the palette:
    (85,130), (350,405), (480,405) onto (1000, 470..650)), then "You must give your creation a name"
    (the name panel opened by "done": its dice at (614, 826) gives a random name). Civ -> Space took
    8 min with the 9 captures.
  * Space start: "Name Your Captain" (dice 915,555, check 936,630), then the host is on its home
    planet, where the game does not save: the host's log says "a player is waiting for the world:
    leave the planet to save" and the guests stay in the Civ stage until the host leaves (mission
    control 290,914 and its five answers, then the wheel out, as space_setup.py does; only after the
    captain's name and any caption, else the clicks are lost).
  * Finale: a host the Space play left on a planet must take off before the wheel takes it to the
    galaxy view (`coop.space finale` ctx 0 = planet, 1 = solar system, 2 = galaxy).
* Debug runs (lane 7, main 8f1944b + this branch): 20261029-000344 (Cell -> Creature, 7 min),
  -001119 (Creature -> Tribe: BUG-0258, then the jump), -002618 (Tribe -> Civ: two villages raided,
  the jump, Civ play: OK), -004238 (Civ -> Space -> finale: every player entered the core and got the
  Staff of Life), -011243 (the whole game in 81 min with 2-minute stages: BUG-0258 again, the Tribe
  jump; no crash, fault, freeze, desync or shared-resource difference in any run), -023333 (Civ ->
  Space -> finale with the fixes: RESULT OK, 22 min, the surrender talk answered by hand once, now
  automated). The tool problems those runs showed are fixed on the branch (see its commits).
* Expected duration of the whole game with the defaults: start ~1 min, 5 stages x 8 min of play,
  Cell ~1 min and Creature ~3 min changes, Tribe 20 min and the jump (~1 min), Civ ~8-10 min,
  the finale ~6 min, follows and rejoins a few minutes: about 2-2.5 h. The lab's timeout for it is 5 h
  (suite field `timeout`).

## Tribe: the live Creature -> Tribe change with two players (2026-10-29, fix/w5-snapshot)
<a id="testing-tribe-snapshot"></a>

* `coop_tribe_snapshot.py` (suite `tribe_snapshot`, BUG-0258): the `creature` fixture, the host's
  evolution bar full (`AddEvolutionPoints 3000`, the only shortcut), then the players' way: History
  and its check, the guest's real "I'm ready" (F9), "done" in the editors, the captions of the fire
  cinematic and the Tribe intro. Waits until the guest is together with the host in Tribe (without
  the fix this already fails when the game's own rename to `snapshot.spo` came after the host's
  last save, as in lab run 20261029-062544). Then it forces the rename the game does (`mem.write` of
  "snapshot.spo" into the name's UTF-16 buffer, same length as the fixture's "Scalinus.spo"; the
  next save sets it back) and checks for 40 s: the same place on both, `together 1 1` all the time,
  a puppet of the guest on the host, the guest does not hide the host.
  `SPORE_FIX_OFF=world.snapshot_name` (or `--nofix`) must FAIL. About 8 minutes.
* Pitfall: the campaign's noise "pause" can leave the host's pause menu open when the stage change
  starts; the History clicks then do nothing and the advance times out (run 20261029-060328 on
  lane 3). `--noise 0` avoids it.
* `coop.avatar` ends with `game_name '<CurrentGameName as is>'`; `world '...'` is the name the mod
  uses (the planet through `snapshot.spo`).

## Creature: leaving the stage for a shared world (2026-10-29, fix/w6-avatar)
<a id="testing-leave-avatar"></a>

* `coop_leave_avatar.py [--rounds N] [--cell]` (suite: `leave_avatar`, `leave_avatar_cell`): the host
  saves and shares N times while the guest plays; `--cell` starts from the Cell fixture, finishes the
  Cell stage, the host's first creature editor and the share after it (the campaign's way). Checks
  on the guest's game per frame of the leaving window (`coop.leaving`): the entity the mod used was
  still in the game's object list. `SPORE_FIX_OFF=world.leave_avatar` is the twin, but the bug did
  not show with 2 players (BUG-0264: no-fail-run), so both sides pass today.
* Timing: a share round takes ~15 s (leaving 0.7 s, menu settle 3 s, load, rejoin).

## Cell: the stage ends while the consoles keep asking (2026-10-29, fix/w6-cell)
<a id="testing-cell-evolve-poll"></a>

* `coop_cell_evolve_poll.py` (suite `cell_evolve_poll`, BUG-0271): the `cell` fixture, four poller
  threads (own console connections) ask both games `coop.status`, `coop.avatar` and `coop.sync` as
  fast as they answer (~3 600 calls per game) while the host ends the Cell stage the players' way
  (food 1000, captions, History, its check) and both get into the Creature stage. Fails when a game
  faults, dies or freezes. `SPORE_FIX_OFF=core.stale_local` must FAIL (3 of 4 runs did, the host or
  the guest at modes::cell_fill +0x54 or game::species_profile). About 2 minutes (4 when it fails).
* The window is the game's own loading step between two frames; the test catches it only because
  the console commands run from the game's message pump there. `coop.cellgone` shows whether a run
  reached it (`local_refused`, `puppet_refused`, `pool_gone`).
* Pitfall: two `lab.py run` started in the same second from one worktree get the same run id and
  write into the same `out/lab/<id>/` (logs mixed); start them a few seconds apart.
  CORRECTION (2026-11-04, fix/w42-infra): fixed by BUG-0353 / BUG-0362: `lab.new_run_dir` takes the
  directory with os.mkdir and gives a run of the same second the next suffix (`<id>-2`, `-3`; with a branch
  `<time>-2-<branch>`), flaky.json is written under a file lock; lab_selftest `same_second` checks it.

## Votes during a caption: coop_vote_caption.py (2026-10-29, fix/w7-vote)
<a id="testing-vote-caption"></a>

* `tools/lab.py run vote_caption --lane N` (2 players, cold, ~70 s after both games are up): the
  guest plays CRG_ClaimNest (PlayCinematic by hand, the caption holds the input lock: the test
  checks `input_lock` > 0 in `coop.uistate`), the host starts `coop.vote test` twice in a row and
  the guest really clicks "I'm ready": each vote must end "everybody is ready", not by its timer.
  Then EndCinematic frees the lock (Enter does not end a hand-started CRG_ClaimNest) and a third
  vote checks the ordinary path; `locked_native` must stay 0 (no click pressed twice).
* `SPORE_FIX_OFF=core.ui_locked_clicks` fails the first two votes (BUG-0273, lab
  20261029-143459).
* The campaign bot answers a vote before captions (coop_campaign.py `service`): with a caption up
  its clicks hit the input lock; it also toggles the button on every pass (`set_ready(!ready)`).
## Creature: NPCs the host's game pools (2026-10-29, fix/w7-npc)
<a id="testing-npc-pooled"></a>

* `coop_npc_pooled.py` (suite `npc_pooled`, BUG-0274, ~2 min): creature fixture, the guest stays
  at the nest, the host walks 300 m away in 20 steps and waits 30 s. Every 0.6 s both censuses:
  each guest proxy must match a host NPC of the same net id that is in the world (not at the
  planet's centre); one without in two samples in a row is a ghost. `--without-fix` or
  `SPORE_FIX_OFF=npc.pooled`: 12-13 ghosts (FAIL); with the fix none. It also prints the matched
  proxies (median distance to the host's NPC 0.1-0.2 m).
* The campaign census (`census.py`, `campaign_lib.census_problems`) needed no change: a pooled
  object of the host has no net id now (`own=local`), and `diff` only pairs ids.
## Sync per player with four players (2026-10-29, fix/w7-sync)
<a id="testing-sync-per-player"></a>

* `SPORE_LANE=N python3 tools/tests/coop_sync_multi.py --fixture cell|creature --rounds 40`
  (~4 s per check, ~10 min with the start): 4 players, the stage's play bot on every machine, each
  guest's cells / NPCs against the host's path in a rotating order; per guest the median and
  largest p90, the checks over the limit and the frame rate. `FPSLIM=Carol:66` forces 15 fps on a
  guest, `--jsonl` keeps every object's error. A measurement, no pass / fail (BUG-0275).
* coop_multi.measure compares a guest with the host's path over the 0.25 s before its sample and
  after it: the guest shows NPCs 0.15 s late on purpose, a host sample taken 10 ms before the
  guest's counted that lag as a desync. coop_multi.over_limit: out of sync when 10 % of a kind's
  objects and at least two are beyond the limit; a guest often has fewer than 10 NPCs within 90 m,
  and p90 of fewer than 10 is the largest error (one object off failed the kind).
* Measured on lane 6 (65 fps, 4 games): the 3rd and 4th player (instances 20+N / 30+N) are as close
  to the host as the 2nd (Creature p90 median 0.3-0.7 m, Cell 0.01 m). Cells trail with the frame
  rate (15 fps: 283 of 13165 cells over 1.2 m, BUG-0282). Extra instances share grid cells with
  other lanes' games ((instance - 1) mod 24: 36 with 12, 37 with 13, 27 with 3); in lane 6 the
  shared cell did not lower the frame rate.
* The campaign prints every guest's p90, object count, worst object and each game's frame rate
  next to a desync finding (`sync: ...`).


## Support diagnostics test (2026-10-29, feat/w7-diag)
<a id="testing-support-diag"></a>

* `coop_diag.py --fixture cell|creature` (suites `diag_cell`, `diag_creature`, warm, ~1 min each):
  the guest's session clock agrees with the host's (best of 5 reads, under 50 ms beyond the time
  between the two console reads; measured 0.2-12 ms), `coop.say /bug <text>` gives "marked for the
  report" and a `MARK by 'Bob'` line in both games' logs, `tools/merge_logs.py` over the logs of
  the test gives one timeline in order with both MARK lines less than 0.25 s apart, `coop.diag sync
  now` gives `sync-check: OK` in the stage on both games, `coop.diag sync force shift` on the guest
  a `sync-check: MISMATCH ... p1 100.0` on both after the 3 s recheck, the check under 1 ms on each
  game and the diagnostics under 0.05 ms per frame on average (printed as `COST` lines), `env` lists
  the game files. The test reads only the part of each log written after its start (warm runs keep
  the logs of earlier tests).
* The MARK lines of host and guest are not in a fixed order: the guest's clock is the host's within
  a few ms (half the best round trip; frames quantize it), the host logs the mark one frame after
  the guest. Do not test "guest first".
* Unit tests: net_tests `test_diag_*` (messages, the clock estimate, the compare and the recheck,
  MSG_DIAG through a host to a new and an older guest, the connection's loss / bytes / round trip
  statistics incl. a bulk transfer that outruns the ack window: 0 lost), fuzz_decoders `diag *`
  (mark, clock answer, result, digest), tools_selftest `mergelogs`.
## Creature: a new guest in its own editor of the first Creature vote (2026-10-29, fix/w7-legless)
<a id="testing-legless-guest"></a>

* `coop_legless_guest.py --guest done|cancel|new|replace|dontsave` (suites `legless_guest` = new,
  `legless_guest_replace`, `_done`, `_cancel`, `_dontsave`, BUG-0278): the `cell` fixture with a guest
  profile without creations; the host ends the Cell stage the players' way, the guest clicks "I'm
  ready", the host adds legs; the guest does one thing in its own editor (legs + the save dialog's
  button, or done, or cancel + Quit). Checks the guest's avatar species (`coop.avatar`) and the
  species the host's game shows for it (`coop.status` remote 1), after the editors, after a rejoin,
  after the next own editor (`coop.vote editor`, a part, done, Save New) and after "the world's own"
  in the co-op window's list (`coop.creations prev creature`, what the arrow runs). About 9 minutes.
  `SPORE_FIX_OFF=creations.pre_editor` must FAIL on `new` and `replace`.
* Pitfalls: the guest's own editor is the "edit a creation" editor: done after an edit and the red
  cross after an edit open dialogs (coordinates in the test, docs/re/creations.md#creations-own-editor-save);
  a click under a dialog does nothing. A guest may play the host's creation under the host's key
  (290D...) or a remapped one (7D... from `coop.creations` "host species X = Y here"): compare with
  both. Right after an editor the avatar hatches again and `coop.avatar` has no key for a while (up
  to a minute).
## Support logging test (2026-10-29, feat/w7-trace)
<a id="testing-trace-log"></a>

* `coop_trace_log.py` (suite `trace_log`, ~6 min, two cold starts): Cell fixture, the guest eats up
  to three plants / meat of the host's pool with the game's eat handler (`coop.pool eat`), the host
  one; then the creature fixture. It reads both games' `sporekit.<inst>.log` from the start offsets
  (lane.game_log) and checks: the same object `c<K>` in the guest's `eat: sent gone` and the host's
  `eat: from <id> gone ... applied`; periodic `trace:` lines with a partner line `puppet=shown`; a
  `cell:` line with `diet=` and `screen: plants=`, the guest's with `host d=`; `body: puppet of`
  lines; in Creature `creature:` and `body: creature: own=` lines on both, the host's puppet line
  naming the species the guest's own trace reports and built from the guest's creation; the
  partners' versions; the log growth under 30 KB a minute per game.
* `coop.log detail 0|1|2` switches the level at runtime, `coop.log trace` writes the trace lines on
  the next frame (tests).
* `make test`: `text_tests` checks the rate limit (`trace_rate.h`), `net_tests` the version
  extension with an old-shape peer (`test_peer_versions`) and the address masks (`test_ip_mask`).

## A load from the galaxy menu with the old world's player left there (2026-10-29, fix/w8-planet)
<a id="testing-menu-stale-player"></a>

* `SPORE_LANE=N python3 tools/tests/coop_menu_stale_player.py [--nofix] [--rounds 3]` (suite
  `menu_stale_player`, ~10 min): the host plays the tribe fixture and jumps to civ_npc and back to
  tribe (fresh slots 10/11) `--rounds` times, each time with the galaxy menu's player reset patched
  out (`mem.write` of 5 NOPs at 0xB32CF0, GOG 2024 bytes checked first, put back once the menu is
  up), the stand-in for the game fault that skipped it in the campaign (BUG-0284, BUG-0285).
  Checks: the host reaches each stage with an avatar, the guest follows, no fault or crash, and
  `coop.status` "stale menu player cleared N". `--nofix` (or `SPORE_FIX_OFF=world.menu_player`):
  the host crashes in the first load at 0x00BBABE2 `cStarRecord::GetPlanetRecord+0x12`.
* Pitfall: `mem.ptr <addr> <off>` prints only the first hop here; read SpacePlayerData with
  `mem.read` ([0x16DDA8C] then +0x18 / +0x1C).
## The live stage change with four players: campaign_tribe, campaign_space (2026-10-29, fix/w8-stuck)
<a id="testing-campaign-stage-change"></a>

* `tools/lab.py run campaign_tribe --lane N` (`coop_campaign.py --from creature --to tribe --minutes
  2 --no-finale`, 8-15 min) and `campaign_space` (`--from civ --to space`, 13-27 min): one stage
  change of the campaign with four players, in the `campaign` suite only. The bug they catch
  (BUG-0276, BUG-0287: the share save posted in the first frames of the new stage or place) is a
  race: with `SPORE_FIX_OFF=world.share_settle_reset` campaign_tribe got stuck in 1 of 2 games
  (lab 20261029-192222); use `--repeat`. A stuck finding of `tribe/follow` or `space/follow` is the
  bug; other findings (a desync, a Civ -> Space change that runs out of time) fail the run too, so
  read the finding.
* The host's log says when each share save was posted: "world: saving for the share of player N
  (<stage>, X s in this place)"; below 2 s means the settle clock did not hold.
## Address lookup test (2026-10-29, fix/w8-adapters)
<a id="testing-adapters"></a>

* `coop_adapters.py` (suite `adapters`, ~2 min with the fix, `reports=True`): two games of one lane
  run `coop.adapters stress 300` at the same moment, three rounds, then the guest leaves, arms
  `coop.adapters fault` (the next lookup reads 0xD54CF6CC through ucrt's memcpy, like Wine's nsi
  did) and opens the co-op window, whose refresh asks for the addresses. Checks: both games alive,
  no crash report, the guest's frames go on, `faults=1`, `main_thread_lookups=0`, its address list
  kept. `SPORE_FIX_OFF=core.adapters` FAILS in the stress part (a natural crash, not the injected
  one). The fix-off run takes ~7 min (the crashed game's report times out).
* `coop.adapters` prints the counters (wine, socket lookups, lookups, failed, faults, main-thread
  lookups, mutex timeouts / waits, the age of the list) and the addresses with their broadcast
  addresses.
* Pitfall: two games of one bottle that call `GetAdaptersAddresses` together crash Wine 10.0
  (docs/04-netcode.md#netcode-wine-nsi). A test helper that looks up addresses must not run in
  every game of a lane at the same moment.

## Core wave 9: the host's world rule, DNA and money of a guest elsewhere, the share retry (2026-10-30, fix/w9-core)
<a id="testing-w9-core"></a>

BUG-0231 (GitHub #165), design in [07-modes](07-modes.md#modes-host-world). Each test FAILS with its
switch off (`SPORE_FIX_OFF=<switch>` or the test's own flag) and PASSES with it:

| Test | What | FAIL with |
| --- | --- | --- |
| `coop_share_retry.py` (suite `share_retry`, Creature) | the guest joins from its own copy of the planet (Otherworld); the host's share of that join fails once after the save; within 120 s the guest plays the host's world | `SPORE_FIX_OFF=core.share_retry` (the guest stays in Otherworld) |
| `coop_civ_other_world.py` step 4 (suite `civ_other_world`) | the guest's own Civ world has 777 more money than the host's (`coop.res add money 777` offline); for 40 s in the session its game's money never becomes the host's total and the host's does not move by it | `SPORE_FIX_OFF=res.elsewhere` |
| `coop_tribe_other_world.py` (suite `tribe_other_world`, Tribe) | the guest in its own copy of the Tribe planet (Otherworld) orders one of its own members to walk 20-80 m (`coop.tribe walk`): it moves more than 8 m in the guest's game | `--nofix` (`coop.fix core.world_gate off`: AI skipped, orders sent to the host) |
| `coop_join_other_world.py` step 5 (suite `join_other_world`) | `coop.dna add 20` on the guest in Otherworld: its game's DNA rises by 20, the host's does not; after `coop.share` it plays the host's world with the host's total | `SPORE_FIX_OFF=core.world_dna` |

Hooks: `coop.worldfix failnext` (host): the next `share_now` fails once with an error that is not
"planet file missing". `coop.status` prints "host elsewhere=0|1, ..., own-world dna gains N, welcome
world XXXXXXXX, share retries N"; `coop.res` prints `held=0|1/N` (the hold, host totals ignored).
Unit: `validate_tests` test_host_world (the classifier, DNA rows 1-8 and 12, the ledger hold).
Pitfall: Civ money moves with the planet's own income during the 40 s: the money check compares
guest and host per sample (they stay ~777 apart), not a fixed value.
Pitfall: with the machine loaded (load average ~25, several lanes) the two-game Monitor reported
"guest: the console stopped answering (crash?)" at the end of civ_other_world / tribe_other_world
(4 of 7 lab runs) while every check passed, no crash report was written and the guest's console
answered right after; a 1 s poller beside a standalone run saw no gap. Same signature as BUG-0261.
## Space world identity, dead modal window, rivals landing (2026-10-30, fix/w9-space)
<a id="testing-space-world-identity"></a>

* `coop_space_trade_reload.py` (suite `space_trade_reload`, cold, ~1.5 min; BUG-0225): the host
  gives 5 bomb charges to a guest on `trade hold 1`, leaves for the menu (the quit saves), loads
  game B (a copy of the fixture slot as `Game7`+, made by the test after the start and removed at
  the end), the guest leaves: nothing may be paid into game B; back in game A the 5 come back
  once (12). `SPORE_FIX_OFF=space.owed_id` FAILS (game B gets a bomb with 5, game A keeps 7).
  `--same-game` (suite `space_trade_reload_same`): game A loaded again, paid once (both versions).
* `coop.space world`: the Space world epoch, `open`, the number of saves of the Space world the
  ledger noted and the game key (`<slot dir>\<planet>.spo`).
* Pitfall: `coop.menu` is the game's quit and SAVES first; a "reload of a game that still has the
  items" cannot be made with it (only the unit test `tests/space_tests.cpp` covers that row).
* `coop_dead_modal.py` (suite `dead_modal`, warm, ~1 min; BUG-0270): `coop.deadmodal` on the guest
  puts a destroyed object (vtable slots 0x65) as the window manager's modal window, then `move`
  sends mouse messages. `SPORE_FIX_OFF=game.dead_modal` FAILS with the game's fault at 0x65; with
  the guard the "dead modal windows dropped" count of `coop.status` rises by one and `exc` stays 0. The state is
  simulated: the game's own way to leave a destroyed modal window is not known.
* `coop_space_rivalry.py` (BUG-0262): lands whoever is still in the system twice (two
  `land_retry` rounds), then waits until both ships are on the home planet and the host's game has
  the guest's puppet before the PvP checks; the failure message names both places.
## Tribe wave 9: bounce back, `coop.tribe act`, coverage detail (2026-10-30, fix/w9-tribe)
<a id="testing-tribe-w9"></a>

* `coop_tribe_bounce_mirrors.py` (suite `tribe_bounce_mirrors`, ~1.5 min warm, ~2.5 min cold,
  BUG-0288): knocks the host's player tribe out (`coop.tribe pincer <member> <enemy> 300`), waits up
  to 40 s for the game-over caption (`coop.ui find "Alas"`: on a cold lane it took longer than the
  15 s of coop_tribe_gameover.py once, and without the caption the test proves nothing: it is a
  check now), presses OK on the host, then compares the player tribe sizes of both games and the t
  keys of `coop.sync`. `SPORE_FIX_OFF=tribe.mirror_out` must FAIL.
* The guest's `tribe lifetime` line has `mirror_out_passed` (snapshots in which the guest spawned a
  mirror into a player tribe without health, the BUG-0288 case) next to `spawns_blocked`.
* `coop.uistate` prints `input_lock=N` (the game's input lock, > 0 under a caption or cinematic).
* `coop.tribe act <action> <member id>` refuses the object actions 3 (GrabTool), 12 (Party), 15
  (RaidHuts) and 24 (Repair) on a member: they take a tool or a hut and faulted the host (BUG-0260).
  Attack a member with 7, an animal with `act_npc 10`. `coop_tribe_act.py` (suite `tribe_act`).
* The campaign's coverage finding (coop_campaign.py) writes a `sync:` note with the coverage and the
  missing keys per kind of every short guest (`coop_multi.measure(..., missed={})`).
* **SporeKit's console serves all clients on one thread** (sporekit/console.cpp, select loop) and
  runs `SK_CMD_ANY_THREAD` commands right there, the waiting ones too (`wait.frames`, `shot`:
  `SK_CMD_NOT_ON_MAIN`). While one connection waits in `wait.frames 300 60000`, every other
  connection's command waits behind it: coop_phase.py's `wait.frames 300` on the guest held the
  Monitor's `frame` for more than its 10 s timeout on a busy lane (3 of 6 runs on lane 8; the guest
  drew on, no hitch in its log), the old Monitor called that "the console stopped answering
  (crash?)" (BUG-0261). The Monitor now asks a fresh connection for up to 30 s and goes on when it
  answers (a line "busy, not a crash"); the frame check still reports a freeze.
  `SPORE_MONITOR_STRICT=1` gives the old verdict. Keep `wait.frames` counts small in tests that
  run a Monitor, or wait with `time.sleep` + `frame`.
## Cell: the host away, a timed editor after a knockout (2026-10-30, fix/w9-cell-away)
<a id="testing-cell-away"></a>

* `coop_cell_away.py` (suite `cell_away`, ~1.5 min, cell fixture): the host goes to the main menu
  (`coop.menu`), the guest's creatures must swim by their own AI (positions of `coop.pool npcs 80`
  6 s apart, at least 2 moved > 1 m), the guest eats 10 points (`coop.pool food 10`), the host
  reloads its save (`coop.load`); then every creature around the guest is a mirror of a live host
  cell again and both food totals are the host's old total + 10. `--editor` (suite
  `cell_away_editor`): the host opens its own cell editor instead (mating call, `coop.pool mate tp`)
  and cancels. `SPORE_FIX_OFF=pool.away` FAILS both.
* `coop.pool mate tp` (tests) puts the own cell onto its mate: a click-swim to a mate off the screen
  (host side, coordinates below the window) did not reach it. The editor opens ~10 s later.
* `coop_cell_editor_timed.py [--revive]` (suites `cell_editor_timed`, `cell_editor_timed_revive`,
  ~1.5 min each): a real respawn (25 m from the host) or revive (next to it), the cell onto its mate
  the moment the knockout ends, then the editor: prints when the stage was left (always ~10 s, after
  the 2-3 s invulnerability), checks +0x111 clear and a host bite after the return (BUG-0237 guard).
* `coop_cell_respawn.py` also checks the knocked-out partner's death count S+0x70 (`coop.pool
  deaths`), BUG-0221.
* Pitfall: the AI lock byte +0x16C is not a usable oracle of "frozen" (it reads 0 on mirrors between
  frames); check positions over time.
* Pitfall (2026-10-30): `coop_cell.py` (and others) read the first `food=` of `coop.pool`. A new line
  in that output must not repeat a key of the status line before it (`cell_away:` first had
  `food=0/0` there and cell_coop failed with "guest has the same total (0)"); new lines go last.
## Creature wave 9: far guests, rejoin herds, the guest's open editor (2026-10-30, fix/w9-creature)
<a id="testing-w9-creature"></a>

* `coop_npc_far.py` (suite `npc_far`, ~2.3 min cold, BUG-0280): the guest stays at the nest, the
  host is teleported 450 m away in 20 steps (`coop.npc gopos`), 8 s for the relevance pass, then
  40 s of censuses of both games: the guest's proxies within 120 m of its avatar (at least 85 % of
  the count before), the host's NPCs there, new net ids near the guest. Without the fix (lane 9):
  20 -> 8 or 12 NPCs left; with it 20 -> 20. `coop.views` (host) prints the partners' centres and
  the game's own sphere: in the headless lanes the host's camera does not follow a `gopos` all the
  way (its sphere stayed ~370 m behind an avatar sent 600 m away), so how many NPCs the fix-off run
  loses depends on where the camera stopped; the nest flicker of BUG-0274 (new ids every 3-6 s)
  was not seen in these runs (0 new ids either way).
* `coop_rejoin_saved_puppet.py` (suite `rejoin_saved_puppet`, ~2.5 min cold, BUG-0256): a passed
  vote, the guest leaves with its editor due (`core.own_editor_end off`: the editor opens offline,
  as on 0.7.3.0), the host leaves its editor, the guest joins again and loads the share; 20 s of
  play. Fix off: the guest removes the saved creature of its species twice and faults every frame
  at 0x00C0B8D2 (1229 faults in one run). `coop.npc herdnulls`: how many null members the
  Herd_Update hook took out.
* `coop_guest_editor_hold.py` (suite `guest_editor_hold`, extended, cell fixture, ~4 min cold,
  BUG-0283): the first Creature vote from the Cell stage as in `coop_legless_guest.py`; the guest
  leaves its own editor alone for 40 s after the host's new creation reached it (its game log),
  then one click on the red cross must leave the editor. Not reproduced in 3 runs on lane 9 (the
  click 3 s after the editor came up, 5 s and 40 s after the host's body): the editor kept the
  legless creature with 115 DNA and left on one click every time.
* `coop_tribe_far.py` (suite `tribe_far`, tribe fixture, ~2 min cold, BUG-0280 Tribe and BUG-0291):
  the guest's pointer is pinned (`coop.npc marker x y z`) at the home of a herd of the game 300-450 m
  from the host's camera sphere; 40 s of `coop.npc archless all` (herds within 100 m enabled, members
  near their homes) and `coop.npc list` on both games (the host's NPCs within 100 m of the point and
  the guest's proxies of them). `SPORE_FIX_OFF=npc.view_centres_tribe` fails both checks,
  `SPORE_FIX_OFF=npc.rts_views` only the BUG-0291 one. `coop.census` lists no creatures in Tribe
  (Creature and Civ only), so the test reads `coop.npc list`.
## Three and four players per stage: party.py, coop_party.py (2026-10-30, fix/w9-tests)
<a id="testing-party"></a>

* `tools/tests/party.py` (BUG-0152, GitHub #131) is the helper for every test with more than two
  players, and the base of the 0.8 Rivals tests (docs/11-rivals.md). `Party.start(fixture, players=3|4)`
  starts lane N's host (Alice) and guest (Bob) with `twoinst.start`, then one or two extra guests in
  the same bottle (Carol = instance 20+N, Dave = 30+N, empty profiles `mp1` / `mp2`,
  `coop_multi.start_extra`) that join at the same moment (`join_together=False`: one after the
  other). Each machine is a `Player` (`name`, `con`, `inst`, `pid`, `join`).
* Checks of every machine's own game: `check_together` (each guest plays the host's stage with an
  avatar and the host counts it as together; Space: in the session), `check_partners` (each guest's
  game shows every other player, guests too, near where that player's own game has it:
  `partner_view(viewer, subject)` compares the viewer's `coop.sync` key `p<id>` with the subject's
  own avatar; limits per stage in `party.NEAR`), `check_res` (shared food / money), `faults`.
* Actions: `leave`, `join` (waits until it is back in the host's world; never presses Enter on the
  host's "game saved" box: the mod closes it, a press could hide a stuck box), `vote_ready` (a
  real click on "I'm ready" in the player's co-op window), `vote_line` (the vote line of that
  machine's window, "... - ready k/N - s"), `host_away` (`coop.menu`, until every guest shows
  `host_away=1`), `host_back` (the host loads the lane's slot again), `stop` (ends the lane's
  games: the extras share the bottle, so these tests are cold and mark the lane state not reusable).
* `coop_party.py --fixture <stage> --players 3|4 [--one-by-one] [--skip vote,rejoin,away]`: join
  (two guests at once), see (every guest sees every other player, shared totals), vote (`coop.vote
  action`; all guests but the last ready: every window shows the same ready k/N, the vote waits;
  the last guest leaves: it passes once), rejoin of the last guest, the host in the menu and back.
  Suite `multi` (`tools/lab.py run multi --lane N`, also `extended` / `nightly`): `party_cell3`,
  `party_creature4`, `party_tribe4`, `party_civ3`, `party_space4`, `party_creature3_seq` and
  `multi3` (coop_multi chaos, ~4 min). About 2-2.5 min each (lab 20261030-003549: 6 tests in 6.4 min on
  two lanes).
* The gameplay between guests, one test per stage in the `multi` suite (BUG-0152), each 1-2 min:
  * `party_cell_ko` (`coop_party_cell.py`, 3 players): a guest's knockout drawn translucent by the
    host and the other guest, a revive by the other guest (`coop.pool tp <dx> <dy> <id>` puts a cell
    next to a given player), two guests knocked out side by side revive nobody (#110).
    `SPORE_FIX_OFF=pool.rescue` FAILS (lab 20261030-010849; PASS 20261030-010719).
  * `party_creature_npc` (`coop_party_creature.py`, 4 players): two guests' hits kill one NPC
    (one kill on every table, the NPC dead in every guest's game), two guests impress one NPC in
    the same second (impressed once, the other refused), an NPC next to two partners gets the
    "noticed" stimulus from their puppets only (behaviour tree, `stimuli` of coop_creature_kills),
    a guest leaves while another one receives the world, and while a guest's own creation travels
    to the others. `SPORE_FIX_OFF=npc.play_death` FAILS (20261030-011708), `npc.partners` too
    (20261030-014139; PASS 20261030-010920,
    20261030-012915).
  * `party_tribe_split` (`coop_party_tribe.py`, 4 players): the split of the player tribe and the
    chiefs the same on every machine, a gift from guest to guest that the new owner can select, a
    new split when the fourth player leaves and comes back. No fix switch of these paths exists, so
    no FAIL run.
  * `party_civ_own` (`coop_party_civ.py`, 4 players, `civ`): the shared capital, a purchase per
    guest with the same owners everywhere, a vehicle gift from guest to guest that the new owner
    drives, superweapon requests from two guests in one vote (#96, the game's money), a leaver's
    vehicle. `SPORE_FIX_OFF=civ.sw_vote,civ.orphans` FAILS (20261030-011312; PASS 20261030-011125).
  * `party_civ_trade` (`coop_party_civ_trade.py`, 3 players, `civ_npc`): the host's civ captures
    two NPC cities (three cities, one per player, no shared capital, the same owners everywhere),
    Bob Military and Carol Economic on every machine, one trade payout of three pairs (+100, the
    Economic governor doubles hers), a leaver's city and role.
  * `party_space_names` (`coop_party_space.py`, 3 players, two of them join as "Bob", #120):
    unique names, a tool gift from guest to guest, a contract crew without the host (a 1-star
    survey the two guests fly; the reward once on every machine), allies mirrors of two partners
    (each guest colonizes a star; every other galaxy shows "<name>'s empire"), war and peace between
    two guests in rivals mode with the host neutral, two disabled guests and the host's lethal hit
    (#110). `SPORE_FIX_OFF=core.unique_names,space.helper` FAILS (20261030-011923; PASS
    20261030-011655).
  * The finale with several partners is in the campaign (4 players, every player enters the core:
    lab 20261029-203821 campaign#1).
  * `coop_party.py` (party_<stage><n>): a vote with 3-4 players, a leaver not ready lets it pass,
    a player joining during a vote (the window of the newcomer shows it, it waits for its ready).
* `Party.start(..., join_names={'Carol': 'Bob'})` joins an extra guest under another name;
  `Player.netname` is its name in the session, found by `ids()`. `SPORE_FIX_OFF` reaches the extra
  guests too (twoinst.start switches the host and Bob only).
* Pitfall: `Player.call` returns the text; the raw console is `Player.con`.
* Pitfall: a tribe member's size is no test criterion: GetDesiredScale (citizen vt 0x80) of the same
  member differs between the machines and from moment to moment (chief / own ratios 1.05-1.56 on
  one machine, only the fixture's grown chief stayed at 1.3; lab 20261030-012230, 20261030-012238).
  `coop.tribe scales` lists `id:scale/own` per member.

## civ_reload and civ_sw_vote read the game (2026-10-30, fix/w9-tests)
<a id="testing-civ-checks"></a>

* `coop_civ_sw_vote.py` (BUG-0117, BUG-0222, ~1 min): makes the weapons usable without the free
  cheat (`mem.write` of `unlocked[type]` at player civ +0x484, the civ from `coop.civ sw`; shared
  money 20000 by `coop.res add money`) and asserts the game's money (+0x98) on the host and on the
  guest: the vote fired the cheap type of its own request (2000), not the dear type 1 of the later
  one (16000). Cases `--case guest|host|both`. `SPORE_FIX_OFF=civ.sw_vote` FAILS (guest case: 16000
  spent; host case: 0, type 1 is then on cooldown). Pitfall: a fired type stays unusable for minutes,
  so each case uses its own cheap type; never fire type 4 at the zero vector (crashed the game,
  docs/re/civ.md#civ-superweapon-2024).
* `coop_civ_reload.py` (BUG-0040, BUG-0059, BUG-0222, ~1.5 min): the alarm boost as a precondition
  (the boosted vehicles' +0x218 at 1.5x), the speed back to the vehicle's own 2 s after `coop.leave`
  (`SPORE_FIX_OFF=civ.boost` FAILS: the game keeps 1.5x for 30 s and more), and a forced stale
  vehicle entry (`coop.civ stale_hv <id> 1`, host only, Civ only): the guest's order must not reach
  that NPC vehicle in the host's game (`opos` of `coop.civ`; `SPORE_FIX_OFF=civ.world` FAILS).
* Pitfall: two `lab.py run` started in the same second on different lanes got the same run folder
  (out/lab/20261030-001854 holds both logs, the summary of one). Start them a few seconds apart.

## Cell with four players: cell_4p (2026-10-30, fix/w10-cell-4p)
<a id="testing-cell-4p"></a>

* `tools/tests/coop_cell_4p.py` (suite entry `cell_4p`, suites multi / extended, ~6 min, cold:
  party.py with four games in one lane): `coop.pool food 300` on the host (level 4 of the tile
  levels within ~8 s), the three guests 200 m apart from the host and from each other (`coop.pool tp`),
  60 s, then 50 m around the host, 60 s; the guests swim a little (`coop.pool goto`). Checked on every
  game: the cell pool's highest live count (`peak=` in `coop.pool`, the game's pool header) under
  1800 (the broadphase overflows at ~2048), each guest's coverage of the host cells within 60 m, cells of the host's world on each
  guest's screen; the lab fails it on any fault. BUG-0295 / BUG-0298, docs/re/cell.md#cell-broadphase-cap.
* `SPORE_FIX_OFF=cell.budget` FAILS (lab 20261030-052851: host 4096 cells, 8203 faults at 0xBBB07B).
* Food (plant / meat) on a guest's screen is not asserted: at level 4 a screen with 300+ host cells
  often had none (also without the budget). Use `cell_far_guest` (two players) for food far away.
* The sync-check and `coop_multi.measure` count host cells within 60 m of the guest only (BUG-0298).
* `coop_cell_particles.py` (suite `cell_particles`, 2 players, ~2 min): food 300, the guest's
  soft-body particle pool filled to 1499 (`coop.pool particles 1499`), 10 x `coop.pool zap 5000`.
  Checked: the pool's count in 0..1500, the guest's own cell within 30 m of where it was, mirrors
  back, no fault. BUG-0338, docs/re/cell.md#cell-particle-pool. `SPORE_FIX_OFF=cell.particles`
  FAILS (faults at 0x00E4E384, the own cell 8385 m away).
* cell_sap (2026-10-31, fix/w23-cell): the column takes all the host's cells but 40 (at least 1100)
  and the swim check asks for most intervals and no jump over 60 m; its BUG-0338 faults are fixed.
* `coop_cell_pause.py` (suite `cell_pause`, 2 players, ~2 min): food 300, the guest opens its pause
  menu (Esc, "Resume Game" shown), 12 x `coop.pool zap 5000` 2.5 s apart while paused, Resume, then
  `coop.leave`. Checked: the broadphase's pending count (`coop.pool sap` pending=, the game's header)
  <= 8192 while paused; after Resume pending 0, no object left open, no duplicate entries; mirrors
  back; the guest's game answers after leaving Cell; no fault. BUG-0348,
  docs/re/cell.md#cell-sap-pending. `SPORE_FIX_OFF=cell.sap_pending` FAILS (pending ~20000).
* `coop_cell_sap.py` (suite `cell_sap`, multi / extended, ~2 min, four players): food 300 on the
  host (~1300 cells), the guests 200 m away, then `coop.pool column 1100` (1100 host cells on one
  40 m line next to the host's cell): the game's broadphase sweep holds them all open at once.
  Checked: the host's cell still swims (`coop.pool goto`), no hitch over 400 ms, `coop.pool sap`
  `sweep_peak` > 1024 (the case was reached), each guest's game shows the host's cell swimming
  (its puppet `p0` in `coop.sync`), no fault.
  BUG-0337, docs/re/cell.md#cell-sap-active-list. `SPORE_FIX_OFF=cell.sap_sweep` FAILS (the host
  faults every frame at small addresses with esp=0012D394, its cell stops).
  Flaky (2026-10-31, fix/w22-cell): the pack sometimes reaches only ~1012 open objects (the cells
  spread before the next sweep) and sometimes trips BUG-0338's advect-grid fault on the host
  (lab 20261031-124047-fix-w22-cell, 20261031-124835-fix-w22-cell #1).
* `coop_cell_nan.py` (suite `cell_nan`, 2 players, ~1 min): the guest's own cell NaN
  (`coop.pool nanself`), back at a finite position within 3 s, then `coop.pool food 1000` on the
  host (level-ups). BUG-0299; `SPORE_FIX_OFF=cell.nan_guard` FAILS (lab 20261030-055502: the
  position stays NaN, the guest hangs after the level-ups).

## The captain screen blocks the wheel (space_captain, 2026-10-30, fix/w11-finale)
<a id="testing-captain-screen"></a>

* The rank badge over the ship's picture (1390 890) opens the captain screen; while it is up the
  wheel does nothing and `coop.space finale` still says busy=0. Esc does not close it (it opens the
  pause menu over it): click its OK (`play_space.close_captain`). Detect it with
  `play_space.captain_open` (`coop.ui find Equipment` shown).
* Bots must not click objects projected under the badge (play_space.Player.targets skips
  x > W-120, y > H-225). The 4-player campaign lost the finale to it (BUG-0303).
* Each opening logs the game's own caught fault at 0x5971D4 (BUG-0304): `space_captain` is
  `reports=True` and accepts only that one. `OLD_FINALE_TOOL=1` keeps the old `to_galaxy` (fails).
* `coop_sync_multi.py --fixture tribe`: four players in the tribe fixture with play_tribe bots (the
  campaign's setup: `coop.tribe spawn 3 x players`, one thread steps every player).
## The start checks that the host hosts (2026-10-30, fix/w11-space-join)
<a id="testing-host-start-check"></a>

* `twoinst.start` checks the answer of `coop host` (`host_up`): a "host failed: ..." is printed and
  tried again (3 times, 3 s apart), and a guest that is not in the host's world after 240 s is
  printed with its `coop.status` line. Before (BUG-0302, lab 20261030-074023 space_coop) a host that
  never hosted left no line in any log, the guest's join timed out after 10 s and the test failed
  4 minutes later on something else ("the host has an NPC ship []"). The game log now has
  "net: host failed on UDP port N: <error>" too. The lab's co-op ports (47810-47828) lie inside
  Linux's ephemeral range (32768-60999): any UDP socket of the machine can hold one for a moment.
* `space_share_takeoff` (BUG-0301, ~1.5 min): the guest rejoins while the host is on a planet, the
  host takes off, the share save goes out with the retry every frame (`coop.worldfix retry 0`); the
  guest's own place after the load must be the host's solar system. FAIL with
  SPORE_FIX_OFF=world.share_settle_context (the guest's ship lands on the planet 2.5 s after the load).

## Animals that run side by side: tribe_npc_dash (2026-10-30, fix/w12-tribe-npc)
<a id="testing-tribe-npc-dash"></a>

* `coop_tribe_npc_dash.py` (`tribe_npc_dash`, ~2 min, tribe fixture, BUG-0305): the host makes two
  animals of the small herd species 06808ACA run 1.2 m apart along a 12 m zigzag at 6.5 m/s
  (`coop.npc dash <id> <m> <m/s> <s> [zigzag m] [mate id] [m beside]`, a test command that writes
  their position and velocity after the game's update), the guest at 15 fps (`frame.limit 66`);
  the guest's positions against the host's path (coop_multi.measure) must have p90 <= 1.2 m.
  Fix on: p90 0.7-1.1 m; `SPORE_FIX_OFF=npc.trail`: 1.5-2.8 m. The margin is small (one fix-off
  run had 1.47 m): a pass of a twin run is a reason to run it again, not proof.
* Do not force bigger species that close: a 16DE4FEA pair 1.2 m apart overlaps and the guest flings
  the proxies apart at 12 m/s (both sides fail). The species and the spacing are env knobs
  (`DASH_SPECIES`, `DASH_OFF`, `DASH_SPEED`, `DASH_FRAME_MS`, `DASH_VERBOSE=1` prints the proxy line).
* `coop.npc list` shows on the host `spd= vel= upd=N/64 velfr=N/64 moved64=` (updates, moving frames
  and metres moved over the last 64 frames) and on a guest `at= rx=N/s spd= vel= own= tp= stall= trail=`
  (states per sender second, the sample's velocity, the proxy's own speed and teleports). It used to
  be cut at 8 KB (SporeKit's out_printf buffer): a guest's list of 40 proxies lost its tail.
* `coop_sync_multi.py` prints `coop.npc list` of an NPC more than 1.5 m off on every machine, 5 times
  0.3 s apart (also into the jsonl as `probe`), and applies SPORE_FIX_OFF to Carol and Dave too.
* Two `lab.py run` started within the same second share one out/lab folder (and one log): start a
  twin run a few seconds after the other.
## Cell: a group member's dead chase target, cell_stale (2026-10-30, fix/w12-cell-fault)
<a id="testing-cell-stale"></a>

* `coop_cell_stale.py` (suite `cell_stale`, 2 players, ~2 min): `coop.pool staletarget 16` (host)
  makes up to 16 group leaders chase a dead cell and rest 60 s; within 10 s the host must log no new
  caught fault (`exc`) and keep drawing. `SPORE_FIX_OFF=cell.pick_guard` must FAIL with faults at
  0x00E52949 (IsDamageable, one per frame; BUG-0306). It needs groups in the host's pool at the
  cell fixture: the first check fails when none is found (then the fixture has no packs).
* A campaign's "N new caught fault(s)" counts every fault at one address; the game log shows only
  the first one (sporekit/exceptions.cpp keeps one record per address). Thousands per check at one
  address mean a fault at every frame (Wine aborts the frame each time), not many different bugs.

## Cell: the own cell's NaN fields and the mirror churn (2026-10-30, fix/w12-cell-fault)
<a id="testing-cell-nan-all"></a>

* `coop_cell_nan.py --all` (suite `cell_nan_all`, ~1.5 min): `coop.pool nanself all` (guest) makes
  the own cell's position, matrices and the particles' rest offsets NaN; the own cell must be
  finite at 15 samples over 15 s with at most one NaN episode (`own_nf=` of `coop.pool`), and it
  prints the free address space. `SPORE_FIX_OFF=cell.nan_heal_all` fails (BUG-0307).
* `coop_cell_churn.py` (suite `cell_churn`, ~3 min): the guest removes all its mirror cells 40 times
  (`coop.pool zap 5000`), the host's states create them again; the free address space
  (`vm_free_mb=` of `coop.pool`, GlobalMemoryStatusEx) must stay within 150 MB. A measurement more
  than a regression test (it passed on the first try: the churn does not leak).
* `coop.pool` (cell_contacts line, both roles): `own_nf=` (finite -> non-finite transitions of the
  own cell seen by the probes), `shared_parts=` (own particle indices another cell holds),
  `vm_free_mb=`, `created=` / `destroyed=` (mirrors), `stale_targets=` (BUG-0306).

## LAN list of a host with two addresses, the 15 fps sync run (2026-10-30, fix/w13-net)
<a id="testing-lan-dedup"></a>

* `coop_lan_dedup.py` (suite `lan_dedup`, ~1.2 min warm, creature fixture): the guest leaves,
  opens the co-op window and reads its LAN rows for 12 s (`coop.lanfake 0` prints them with the
  game id); the lane's own host must be one row. It needs a machine with two addresses on one
  network (the lab PC: wired 192.168.1.113 and Wi-Fi .138), else it passes with a note.
  `SPORE_FIX_OFF=net.lan_dedup` fails (20261030-134353: two rows, one per address). BUG-0210.
<a id="testing-sync-lowfps"></a>

* `coop_sync.py --fps N` (suite `sync_lowfps` = `--fps 15 civ tribe`, ~3 min): the guest runs at
  ~N fps (`frame.limit`); in Civ the vehicles are sent on every round (moving all the time). At
  15 fps the windowed errors stay inside the limits (vehicles p90 1.6-3.1 m against 3.0 m: one
  vehicle, 16 samples, the noisiest kind; citizens 0.8 m, animals 0.7 m). It does not separate
  BUG-0175's fix on and off: with the game time running mirrors walk and the 0.15 s lag window
  hides a one-frame lag; the fix's FAIL/PASS is `npc_tests` (`--nofix`).

## A cleaned-up citizen walked as Tribe is left: tribe_leave_walk (2026-10-30, fix/w13-tribe-civ)
<a id="testing-tribe-leave-walk"></a>

* `SPORE_LANE=N python3 tools/tests/coop_tribe_leave_walk.py [--nofix] [--rounds 2] [--play 90]`
  (suite `tribe_leave_walk`, ~7 min cold): the host plays the tribe fixture with the play_tribe
  bots for `--play` s, arms `coop.leavewalk` and loads the civ_npc fixture through the galaxy menu
  (the campaign's jump), then the tribe fixture again. The stand-in: in the first frame the Tribe
  stage is left (quit posted) one player member gets +0xB54 = 0 (what CreatureBase_Cleanup leaves)
  and goes through the guard, as the campaign's citizen did (the game's own path needs a citizen in
  one action as the mode changes, which 90 s of play never gave: docs/re/tribe.md#tribe-offstage-action).
  Checks: the stand-in fired, the host reaches Civ / Tribe with an avatar, the guest follows, no
  fault. `--nofix` (or `SPORE_FIX_OFF=tribe.leave_walk`): the campaign's fault 0xC0DB23
  `CreatureBase_SetMoveScale+0x13` under DoAction+0x496 and the host stops drawing (FROZE) in the load.
* `coop.status` "off-stage checks N (citizens while the stage is left M), cleaned-up citizen walks
  skipped K, test citizens T".
## Cell: jumps, background leftovers and low frame rates (2026-10-30, fix/w13-cell)
<a id="testing-cell-jumps"></a>

* `coop_cell_jumps.py [--who guest|host]` (suites cell_jumps, cell_jumps_host, ~5 min): the food goes
  to 300, the player jumps 40 times 150-250 m, then `coop.pool census` must show at most 600
  background cells (BUG-0314; `SPORE_FIX_OFF=cell.bg_cull` fails with ~1100).
* `coop_cell_4p.py --jumps S [--jump-ms 300]`: the BUG-0307 hunt with four players (Carol jumps at
  ~3 fps and her free address space and pool census are printed every 15 s).
* `coop_cell_nan.py --all --drain S` (suite cell_nan_drain): after the NaN and the level-ups the
  guest plays S s more, its free address space must not drop more than 150 MB (a guard; the drain of
  BUG-0307 did not reproduce this way).
* `coop_cell_lowfps.py` (suite cell_lowfps): a guest at 15 fps compares the host's moving cells
  (BUG-0282 guard; it passed on main without a fix). `coop_sync_multi.py --food N` levels the cell
  fixture up first.
* `coop.pool census`: every used slot of the cell pool by kind (a growing `background=` is BUG-0314).
* `coop_rejoin_loop.py --fixture cell --cell-leave` (suite cell_rejoin_leave, ~3 min): 12 rejoins inside
  Cell, each followed by the host's world and the stage leave; `leave_writes=` of the guest's
  `coop.pool` must stay 0 (BUG-0308 guard; `SPORE_FIX_OFF=cell.leave_guard` gives ~1000).
## Creature: the host away, running proxies, fix switches and warm games (2026-10-30, fix/w13-creature)
<a id="testing-w13-creature"></a>

* `coop_away_refill.py` (suite `away_refill`, ~2 min, cold): the host leaves through its pause menu;
  the guest's own NPCs (census `local`, AI on) within 70 m 10 s later must be at least 60 % of the
  host's it showed there (BUG-0180; `SPORE_FIX_OFF=npc.away_locals` fails with 0).
* `DASH_FIXTURE=creature coop_tribe_npc_dash.py`: the Tribe dash test on the creature fixture (nest
  species 065CBE70, 4 m/s, guest at 15 fps 1.5 m beside the path's end; `DASH_GUEST=0` without
  the guest there). A measurement for BUG-0281: it fails with and without `npc.trail` (not in a suite).
* Pitfall (BUG-0312): `coop.fix` switches live in the game process. Until 2026-10-30 a warm test
  reused games a `SPORE_FIX_OFF` run had left with the fix off and silently ran without it; now
  `twoinst` marks the lane (`fix_off` in lane_state.json) and the next test starts cold. A test that
  switches a fix off by its own command (`--without-fix`) still leaves it off for a warm successor.
* Pitfall (BUG-0317): `coop_legless_guest.py --guest cancel` used to add legs and press the save
  dialog's Cancel (the key `cancel` was in its dialog table); screenshots of those runs show the
  guest's own legs and DNA spend, not the host's.
* The guest's proxies have `+0xB67` = 0: a guest's camera far from them does not pool them (lane 4).
## Space: planet seeding and the first landing (2026-10-30, fix/w13-space)
<a id="testing-space-seed-profile"></a>

* `coop_space_seed_profile.py` (suite `space_seed_profile`, ~2 min after a cold start): the host
  loads the species profile of an unvisited NPC star (`coop.space profile <StarID>`, 0046101B in the
  `space` fixture), the guest does not; both jump there and must have the same planets.
  `--without-fix` (`coop.fix space.seed_profile off`) fails: 8 and 6 planets (BUG-0185).
* `coop_space_jump.py` prints "same" / "DIFFERENT planets" without failing; the game logs carry one
  `space: planets of star <id> generated (seeded <seed>): <n> (... hash=<h> ...)` line per
  generation: compare the hash of the same star in the host's and the guest's log.
* `coop_space_land_early.py` (suite `space_land_early`, ~1.5 min): both players land on the home
  planet with click + wheel right after `start('space')`, must be on it within 40 s; prints
  `coop.space landcheck` every second (the game's landing conditions, docs/re/space.md#space-seed-profile).
  A landing shows `paused 1 sg50 2` for about a second: a bot that clicks the planet again then
  waits up to 25 s for the ship to arrive, which looked like a landing delay (BUG-0182).
* A new test file is only known to the lab of its own tree: run `python3 tools/lab.py` of the
  worktree (`../SporeCoop-wt/<branch>/tools/lab.py run <test> --branch <branch>`), the main
  checkout's lab.py reads main's suite.py ("unknown test").
## A mirrored vehicle thrown off its place by the guest's game: civ_mirror_place (2026-10-30, fix/w14-tribe-civ)
<a id="testing-civ-mirror-place"></a>

* `SPORE_LANE=N python3 tools/tests/coop_civ_mirror_place.py [--nofix] [--seconds 40]` (suite
  `civ_mirror_place`, ~2 min, civ_npc): no command; the fixture's NPC vehicle #2 claims the spice
  node near (-336, 132, 354) by itself. Every 0.5 s every mirrored NPC vehicle's position on the
  guest's game is compared with the host's. PASS: all within 4 m and the vehicle at the node seen.
  `--nofix` (`coop.fix civ.mirror_place off`): about 13 m (BUG-0186).
* `coop.civ pushprobe` (guest, tests): points the vehicles' spatial SetPosition / Teleport slots at a
  logger: every move of a mirror over 2 m by the game is logged with its return address.
* `coop.civ` on the guest: `mirror_place_skipped=N`, `founds_outside=N` (city foundings received
  outside the Civ mode, BUG-0319); per mirror `vel= top= jumps=`.
## tribe_npc_dash under load, civ_ruin_repair (2026-10-30, fix/w14-tribe-civ)
<a id="testing-dash-fresh-states"></a>

* `coop_tribe_npc_dash.py` (BUG-0321): the p90 is taken over the samples whose proxy state was fresh
  on the guest (`coop.npc list`: `age=` at most 0.25 s and `rx=` at least 7 states a second); the
  p90 of all samples and the median frame rates of both games are printed. With fewer than 40
  fresh samples the run prints `RESULT: SKIP` (inconclusive: the machine was too loaded to measure
  the driving). On a lane with the rest of the machine busy the fix-on p90 spread 0.33-1.33 m over
  30 runs (limit 1.2 m); the failing samples had states 0.3-0.5 s old or 4-6 a second.
* `coop_civ_ruin_repair.py [--nofix]` (suite `civ_ruin_repair`, ~2 min, civ_npc): the guest's game
  alone ruins one building of an NPC city (`coop.civ ruinlocal <city>`, tests); within 10 s the
  guest must have the host's ruins and buildings again. `--nofix` (`coop.fix civ.ruin_repair off`):
  the ruin stays (BUG-0324).

## Creature proxies and editors at low frame rates (2026-10-30, fix/w14-creature)
<a id="testing-w14-creature"></a>

* `creature_npc_dash` (suite): `DASH_FIXTURE=creature coop_tribe_npc_dash.py`, 4 m/s, guest at 15 fps;
  `SPORE_FIX_OFF=npc.kinematic` fails (BUG-0281). `DASH_GUEST_CMDS="cmd;cmd"` runs console commands
  on the guest before the dash (experiments).
* `coop.npc pwalk <id> x y z <m/s> <s> [mode]` (guest): walks one proxy with the game's locomotion
  for <s> seconds without the sync; mode bits 1 = MoveTo again every 0.1 s, 2 = set_position to
  itself, 4 = set_orientation every frame.
* `editor_after_swap` (suite, ~2 min): guest at 6.7 fps (`frame.limit 150`), rejoin, editor 3 s later;
  the editor must draw (a screenshot's mean brightness over 25, PIL) and one red cross must leave it.
  `SPORE_FIX_OFF=creations.editor_after_swap` fails (BUG-0318); `editor_after_load` (`--no-swap`) the
  same without an avatar swap. With the fix the editor opens ~45 s
  after the request at 6.7 fps.
* `coop.ui tree` counts the editor's windows even while it draws nothing (BUG-0318): a test that
  wants a working editor checks the picture (`coop_legless_guest.editor_shown`).
* Pitfall: `cd X && (cmd) & cmd2` backgrounds the `cd` too; cmd2 then runs in the old directory (a
  lab run of the main checkout instead of the worktree, 2026-10-30).

## Space: mirror models and the finale wait (2026-10-30, fix/w14-space)
<a id="testing-space-mirror-model"></a>

* `coop_space_mirror_model.py` (suite `space_mirror_model`, ~1 min warm): the host sends made-up
  model instances (`coop.fleet fakemodel 1`) and spawns NPC ships; the guest's game must not fault.
  `SPORE_FIX_OFF=fleet.model_check` fails with the fault 0x00E98DD0 (BUG-0323).
* A lane's profiles carry state between tests: the Sporepedia cache `Pollination.package` (not part
  of a fixture) grows with every game that meets NPC empires; host and guest profiles of a lane then
  differ in NPC ship models. To find which profile file triggers a lane-only failure, swap the
  profile (`drive_c/users/<user>/AppData/Roaming/SporeKit-<profile>`) with another lane's and bisect
  its persistent files (the fixtures restore only `Games/`). Lane 8's host profile from before the
  fix is kept at `<bottles>/SporeLab8/test-bak-bug0323`.
* `coop_space_finale_wait.py` (suite `space_finale_wait`, ~4 min): the host waits at the core, the
  guest only flies to the gate, the host enters first; its `entries_with` must count it as together.
  `SPORE_FIX_OFF=space.finale_waited` fails (BUG-0320).
## sync_lowfps: vehicles drive round their city (2026-10-30, fix/w15-civ)
<a id="testing-sync-lowfps-round"></a>

* `coop_sync.py --fps N civ` gives the vehicles a new order every sample; since BUG-0326 it is a drive
  round the city (35 deg further on a 45 m circle), not away / back: a reversal every sample flipped
  the host's vehicle 180 deg inside the 0.15 s window and the facing check cannot tell the direction
  of such a turn (p90 38-87 deg with one vehicle and 16 samples). `SYNC_VERBOSE=1` prints each
  vehicle sample's facing error and the host's turn over the window.
* `coop_civ_ai_spawn.py` checks `removed_local=` (the guest's own vehicles it killed), not
  `removed=` (also the mirrors of host vehicles that went away meanwhile, BUG-0327).
## Tribe rejoin coverage and the campaign's member probe (2026-10-30, fix/w16-tribe)
<a id="testing-tribe-rejoin-npcs"></a>

* `coop_tribe_rejoin_npcs.py [--rounds 2]` (suite `tribe_rejoin_npcs`, ~3 min): the guest leaves and
  joins again twice in Tribe (loads the host's world), then must have the host's NPCs near it
  (coop_multi coverage at least 85 %, none missing in the last sample). A scenario test for
  BUG-0328, which it did not reproduce.
* The campaign's sync note, when a kind is out of sync, now holds the worst object's line on the host
  and on that guest (`coop.tribe` for t, `coop.npc list` for n; the guest's has how its mirror is
  driven), for BUG-0329.
* `coop_sync_multi.py --fixture tribe --spawn N`: the campaign's member setup first; a tribe member
  over 2.5 m off on a guest is printed with its `coop.tribe` line on every machine.

## The team test in warm runs (2026-10-30, fix/w17-creature)
<a id="testing-team-warm"></a>

* `team` reuses games (warm). Until BUG-0330 every warm run failed the rescue (the previous run's 60 s
  rescue cooldown was still running in the guest's game). Fixed in the mod (`team.rescue_reset`).
* The regen check puts the guest 1.5 m from the host first and waits for "close (regen)" in the
  guest's `coop.team`: the guest was found ~14 m away in 2 of ~60 runs (BUG-0333, open). The test
  prints both positions before the hunt, after the guest's hit and before the regen check.
* Pitfall: a background `lab.py` that you think you killed keeps its lane: check with
  `pgrep -x python3` and `/proc/PID/cmdline` before starting another run on that lane (two runs on one
  lane gave "TimeoutError" and "instance stopped answering" on 2026-10-30).
## Ground height, a paused guest's city, creature_cadian (2026-10-30, fix/w17-tribe)
<a id="testing-tribe-own-height"></a>

* `coop_tribe_own_height.py [--nofix] [--lift 5]` (suite `tribe_own_height`, ~2 min): `coop.tribe lift 5`
  on the guest takes the host's members 5 m higher in its copy of the snapshots (as a guest's ground
  5 m off the host's, BUG-0329); members standing on the host must stand on the guest (own speed
  under 1 m/s in 90 % of the samples) and not be teleported again and again. `--nofix`
  (`coop.fix drive.own_height off`): 37 of 56 moving, 12-14 teleports each.
* `coop.ground x y z` (any machine): the game's ground radius in that direction, the point's radius
  and its height over the ground.
* `coop_civ_pause_city.py` (suite `civ_pause_city`, ~1 min): a city founded while the guest's pause
  menu is open appears there after Resume (BUG-0331); the campaign's census skips paused guests.
* Fixture `creature_cadian`: a full campaign's own Creature world (Cadian); `CAMPAIGN_FIXTURE_<stage>=<fixture>`
  starts a campaign stage from another fixture
  (`CAMPAIGN_FIXTURE_creature=creature_cadian coop_campaign.py --from creature --to tribe`).
* The campaign's sync check notes "ground height differs" instead of a desync when the worst t / n
  object is in place sideways and at the same height over each game's own ground (BUG-0332).

## Campaign finale: guests that start in the galaxy view (2026-10-31, fix/w18-space)
<a id="testing-campaign-finale-galaxy"></a>

* "Fly to Alice" from the galaxy view goes straight to the core's gate; when everybody else already
  waits there, the core opens at once and that guest is inside the core cinematic a few seconds
  later. The campaign's finale bot assumed every guest starts in a solar system (first "Fly to
  Alice" next to the core, zoom out, second "Fly to Alice" to the gate) and got stuck on the second
  step for the last guest (campaign 20261031-000341, Dave). It now reads `coop.space finale` before
  each step: in the core (or at the gate) it stops, in the galaxy view it does only the gate step
  (BUG-0335).
* Replay of the finale alone with 4 players: `SPORE_LANE=8 python3 tools/tests/coop_campaign.py
  --from space --to space --minutes 1 --players 4 --no-rejoins --finale-from-galaxy` (~10 min;
  `--finale-from-galaxy` puts every guest into the galaxy view first; `CAMPAIGN_FINALE_OLD=1`
  runs the old steps and fails with the [stuck] findings of the report).

## Space bots: windows that pause the game without a caption (2026-10-31, fix/w19-space)
<a id="testing-space-stray-window"></a>

* The small HUD buttons at the bottom left (1440x1080 window): (24, 1054) the pause menu, (84, 1054)
  the Sporepedia, (114, 1054) My Collections / Mission Log. The last two pause the game time
  (finale `busy=1`, `coop.space hp` cine=0 movie=0), take no mouse wheel and no Enter; Esc closes
  them. `uifind` does not report My Collections as shown (its visibility flags stay 0 while it is
  up): look at the paused game time instead.
* The finale tools and the Space play bot close such a window (`coop_space_finale.close_stray`);
  `OLD_FINALE_TOOL=1` turns that off. Test `coop_space_stray_window.py` (suite
  `space_stray_window`, ~2 min): the host opens each window with a real click, then must reach the
  galaxy view with the game running (BUG-0336). Without the close the host never leaves its system,
  which stalled a whole 4-player campaign finale for 25 minutes (20261031-024151).
## Tribe chiefs apart (2026-10-31, fix/w21-tribe)
<a id="testing-tribe-chiefs-apart"></a>

* `coop_tribe_chiefs_apart.py [--nofix]` (suite `tribe_chiefs_apart`, ~1 min): `coop.tribe stack` (host,
  tests) puts every chief onto the host's; 6 s later no two chiefs within 1.5 m on each machine, then
  each player walks its own chief 8 m (select + walk) and it must move 3 m on both machines. A
  scenario test for BUG-0340: it also passes with `tribe.spread_chiefs` off, the game's own
  avoidance parts two stacked chiefs within 1-2 s.
* `coop.tribe` member lines end with `radius=` (the spatial's bounding radius).
* (BUG-0340) The campaign checks the players' chiefs after the Tribe arrival and after a Tribe rejoin:
  on every machine no two chiefs closer than the sum of their bounding radii, twice 8 s apart
  (finding `stack`). With 4 players and both switches off it fails at the arrival (seed 777).

## Cell: guest after a reload, a guest's bite ring (2026-10-31, fix/w22-reports2)
<a id="testing-cell-reload-ring"></a>

* `coop_cell_reload_join.py` (suite `cell_reload_join`, cell fixture, ~1.5 min warm, BUG-0352): the
  host moves 80 m away (`coop.pool tp 80 0`), saves, `coop.share`; the guest's cell must be within
  15 m of the host in both games 20 s after the load. Wait ~8 s after the start first: the guest's own
  join after the fixture load moves it next to the host (it is a new avatar too). A loaded Cell
  world keeps the cell at the start while the game's input lock (the hatch) is on: a move made then is
  undone (seen: the join fired 2 s after the load and the cell was back at (100 100)).
* `coop_cell_bite_ring.py` (suite `cell_bite_ring`, ~1 min, BUG-0350): the ring is read from the
  game's cell UI rollover pool (`ring=` in `coop.pool kill` / `coop.pool cell`), the host's own bite
  is the control. A creature that fled has no GFX on the guest for a while after the guest was
  teleported to it: the test waits until `gfx=` is set (the game shows no ring without it). The
  `onscreen=` field (world_to_screen) is not reliable on the headless lanes.

## A guest without the Creepy & Cute pack (guest_missing_pack, 2026-10-31, fix/w24-parts)
<a id="testing-missing-pack"></a>

* `coop_guest_missing_pack.py` (suite `guest_missing_pack`, `uses_free_lane`: Carol runs on the lab's
  free lane with `bp1content/BoosterPack_01.package` of that lane moved aside and put back at the end).
  The host plays Tiger Horse with the pack part 0x17A60A7F. Checked: Carol (no pack) plays the host's
  Creature world, the model her game reads has no pack part and its species profile is built
  (`coop.creations parts 290229B9`: cc=0 species=1), Bob (with the pack) reads the original part
  (cc=1), no fault, no 0x441F3B. BUG-0354, docs/re/creations.md#creations-part-substitutes.
* The lab's free lane is the first of 2..8 outside the run's lanes and `--skip`: to keep a test on
  lanes 8 / 9 use `--lane 9 --skip 2,3,4,5,6,7`.
* FAIL: `SPORE_FIX_OFF=creations.part_substitute` (BUG-0341's refusal: Carol stays in the galaxy menu);
  with `creations.missing_parts` off too: 0.7.3.1 (0x441F3B, the load hangs).

## Cell: a crowd on screen and the soft-body particle pool (cell_spare, 2026-10-31, fix/w26-cell-spare)
<a id="testing-cell-spare"></a>

* `coop_cell_spare.py` (suite `cell_spare`, 2 players, ~2 min): food 300, then the host puts 1000
  of its cells on a disc of 8 m around its own (`coop.pool crowd <n> <r>`): over 256 cells in both
  games' active areas (20 x 15 m at that level), the cell graphics pool's capacity. Checked in both
  games every 4 s for 12 s: `coop.pool orphans` (used slots of the particle pool no cell's list holds,
  the game's own pool and lists; with the fix off 600-1500 within 4 s, all allocated at 0xE5CC88) and
  `coop.pool parts` (cells holding the spare slot 1499, NaN cells; also `gfx=<live>/<capacity>`,
  `soft_refused=`), then the end of Cell (food 1000, the captions, History) with the guest's fps from
  its frame counter (> 10) and the guest in Creature. BUG-0355, docs/re/cell.md#cell-gfx-pool.
  FAIL: `SPORE_FIX_OFF=cell.gfx_full`.
* The crowd itself kills cells in both games, fix on or off: crammed cells are pushed out of the
  world and destroyed by the cell update 0xE7C080 (DestroyCell from 0xE7C3E6, 1000-1700 in 12 s),
  the host's live cells fall to 400-800 and the guest's mirrors with them. Not a fix effect (same
  callers and counts with the fixes off); do not assert on the number of mirrors after a crowd.
* Not reproduced in 2 players: the campaign guest's 3 fps. A 2-player crowd keeps 60+ fps with the
  fix off. Tried and dropped: filling the pool with particles nobody holds (`coop.pool particles`
  after `zap`: only 20-45 cells shared the spare), making the guest's mirrors bigger (cells without
  a graphics object never re-tier), `crowd` with a 25 m disc (bigger than the active area).
* `coop.sample [ms] [top]` (debug.cpp, runs on the console thread: SK_CMD_ANY_THREAD |
  SK_CMD_NOT_ON_MAIN): suspends the game's main thread every ~1 ms and prints its most frequent EIPs
  (`self:`, 16-byte buckets at 0x400000 addresses) and the image addresses found in the top 2 KB of
  its stack (`stack:`, once per sample). Registered with the default flags (main thread) it
  suspended itself and froze the game for good: the watchdog's hang report showed c_sample on the
  main thread. Nothing that takes a lock may run while the thread is suspended (no allocation, no
  ReadProcessMemory: the stack is copied with memcpy up to the TEB's StackBase).
* `coop.pool orphans` also counts DestroyCell calls by caller module+offset and whether they gave
  the cell's particles back; `coop_campaign.py` now passes SPORE_FIX_OFF to the extra players
  (Carol, Dave) too.
* cell_particles (BUG-0355 follow-up): the guest's own cell is invulnerable for the rounds (bites and
  a knock-out's respawn moved it 5-18 m in a round) and may move at most 20 m in a round (16 rounds;
  invulnerable, with the fixes, the crowd still shoved it up to 10.8 m in 1 of 12 runs;
  `--no-fill`: the same churn without a full pool, suite `cell_particles_nofill`, extended). Do not
  trust a handful of smooth runs here: all fixes off gave 4 smooth runs in one lab and 6 of 12 with
  steps in another.

## Creature: the ability bar after the own creature swap (ability_bar, 2026-10-31, fix/w27-creature-fault)
<a id="testing-ability-bar"></a>

* `coop_ability_bar.py` (suite `ability_bar`, 2 players, ~3 min): the guest plays each of its own
  creatures once (`coop.creations choose creature <key>`, waits for `coop.avatar key=`) and reads
  its ability count by calling the avatar's vt 0xB0 through `call`; the biggest one gets a bar built
  by the game (`coop.creations bar rebuild`, the game's own builder, as at a stage start), then the
  guest takes the smallest one. Checked in the guest's game memory (`mem.read` of
  `SporeCoop::AbilityBar_ptr`): no slot past the avatar's abilities, and the bar is the one the game
  builds for this avatar; then W / 1 / 2 / E are pressed and no game may have a caught fault.
  BUG-0358, docs/re/creature.md#creature-ability-bar. FAIL: `SPORE_FIX_OFF=creations.ability_bar`.
* The fault itself (0xD4A397) needs garbage past a species' ability array and is not deterministic:
  with the fix off the slots past the abilities are there every run, the key presses faulted in the
  campaign but not on the fixture (the read found zeros or valid pointers). The test fails on the
  slots, the fault check is extra.
## A share save written on a planet: space_share_landing (2026-10-31, fix/w27-space-fault)
<a id="testing-space-share-landing"></a>

* `coop_space_share_landing.py` (`space_share_landing`, ~1.5 min, space fixture, BUG-0357): the host
  is in its solar system, the guest leaves and joins again, `coop.worldfix landsave 300` lands the
  host's ship (LandOnActivePlanet) 0.3 s after the share save is posted, then the host takes off.
  On the guest's own game after the load: no caught fault (and none in the next 15 s), its ship not
  on a planet, in the host's system. `SPORE_FIX_OFF=world.share_planet_save` FAILS (~1000 faults in
  15 s at 0x00EBDD12, the guest on the planet; labs 20261031-204700, 20261031-204721 with
  `LANDSAVE_MS=700`). A landing in the frame of the post (`landsave 0`) does not reproduce: the
  game drops that save and nothing is written (lab 20261031-204503 passed with the fix off).
* Thousands of caught faults at one address in a campaign's findings with a guest's ship on a planet
  the host is not on: look at the host's log for a "saving for the share" line followed by a
  "stage: Space 80... -> Space 40..." line before its "save: N objects of ours" line.

## What the other player sees, frame by frame: coop.track and puppet_walk (2026-11-01, fix/w29-puppet-move)
<a id="testing-puppet-track"></a>

* `coop.track start|stop|count|dump [from] [n]` (main thread, BUG-0360): every frame in the
  Creature stage, one row for the own avatar (who 255) and one per partner puppet: time, state
  flags, the driver's teleports, the game position after the mod's driving, the position the game
  drew (the puppet's position before this frame's driving: the mod's frame runs at Present, after
  the drawing), the interpolated target, the game's speed of the creature, the partner's reported
  speed, the main animation id, the animation's walk gait state, +0x137, the speed state +0xF98, and
  whether / where it is on the screen (docs/re/creature.md#creature-puppet-walk). A ring of 30 000
  rows; `dump` in pages of 1500.
* Measure the drawn position, not the one after the mod's write: this branch's first numbers read
  after the write and showed SetPosition "exactly on the path" while the drawn puppet trailed 2 m.
* `coop_puppet_walk.py` (suite `puppet_walk`, ~1.6 min cold): one player walks ~25 s by WASD holds
  and clicks on the ground, turning back when more than 12 m from the other (seeded: `--seed N`
  repeats the walk), the other's camera keeps it on the screen (right-drags), the track is read
  back (saved to `out/lane<N>/track/*.json`), both directions. Checks: no frame step beyond 3x the
  walker's top speed (min 0.75 m), in 0.1 s windows at most 5 % standing (< 0.3x the
  speed of the walker's path, the interpolated target: an avatar turning on the spot reports a
  speed) and at least 85 % at 0.6-1.4x, the walk gait on in at least 80 % of the moving frames on
  the observer's screen (at least 150 of them). `PUPPET_WALK_CMDS="cmd;cmd"` runs console commands
  on both games first. `SPORE_FIX_OFF=creature.puppet_walk` fails.
* Pitfalls: a creature behind the observer's camera is animated rarely (its gait value says
  nothing there); the main animation id is no walk measure for puppets; the walker's top speed in
  the fixture is 4.75 m/s; WASD turns (W+D) curve at top speed, where a puppet that only walks falls
  behind; screenshots of the puppet are hard to judge (Kooks of the same colour around, and
  `coop.targets all` gives the tag point).

## NPC proxies frame by frame: coop.npctrack, npc_walk (2026-11-01, fix/w30-npc-walk)
<a id="testing-npc-track"></a>

* `coop.npctrack start [radius m]|stop|count|dump [from] [n]` (main thread, BUG-0361): every frame,
  on a guest one row per NPC proxy within the radius (default 40 m) of its avatar (Tribe: its player
  tribe), on the host one row per own NPC (`how` 4, the reference). Columns: t id how (0 driven, 1
  placed by npc.kinematic, 2 killed, 3 walked by npc.walk / npc.walk_tribe, 4 host) flags tp, the
  drawn position (before the mod's driving), the position after it, the interpolated host target,
  the game's speed, the host's speed, the distance to the centre, the animation id, the walk gait
  (two ints), +0x137, +0xF98, on screen, screen x y. A ring of 200 000 rows; `count` prints the centre.
  `coop.npctrack walk <id> x y z <m/s>` (host): the game walks host NPC #id there (SetDesiredSpeed +
  MoveTo, as its AI does). `coop.npctrack variant N` (guest): how npc.walk moves (0 the fix, 1 place
  only, 2 MoveTo + teleport with the mark, 3 never teleport when the game loses the place).
* `coop_npc_walk.py [--seed N] [--wander s] [--walk s] [--flee s]` (suites `npc_walk`, ~1.8 min,
  creature; `npc_walk_tribe`, `TRACK_FIXTURE=tribe`, ~2 min): 15 s of the NPCs' own wandering, 30 s of
  up to 4 NPCs walked by the host's game to points around the guest (Tribe: around where they were)
  at 2-6 m/s, 15 s after the host hit two of them; the guest's camera keeps one walker on its screen.
  Checks over the 0.1 s windows where the target moves: no frame step on the guest's screen beyond 3x
  the top speed times the longer of this and the previous frame time (the game moves a creature by the
  previous frame's time: 0.74 m between samples 42 ms apart after an 86 ms frame; off the screen the
  game updates far creatures in bursts: counted only), <= 5 % standing, >= 85 % at 0.6-1.4x, walk gait
  >= 80 % of the moving frames on the guest's screen. Windows in which the host's path itself jumped
  (a late state, the extrapolation corrected: 1.3 m in one frame on a loaded guest) are left out and
  counted. Tribe (herd mates push each other, 36-60 fps, more late states): >= 75 % matched, up to 3
  steps under 2.5 m, and the drawn proxy's median distance from the host's path <= 0.1 m (fix on
  0.01-0.03 m, off 0.23-0.33 m). The per-frame rows go to
  `out/lane<N>/track/npc_walk_<fixture>_<time>.json` (with the host's rows and the phases).
  `NPC_WALK_GUEST_CMDS="cmd;cmd"` runs console commands on the guest first (`frame.limit 66`: 15 fps);
  `NPC_WALK_INTRO=1|2` plays the menu intro on the guest first (2: and makes its proxies again).
  `SPORE_FIX_OFF=npc.walk` fails the creature test (gait 3-5 %), `npc.walk_tribe` the Tribe one
  (matched 72-77 %).
* Pitfall: a creature far from the camera is updated in bursts (no movement for 0.5 s, then 1 m at
  once); judge what the player sees (on-screen frames), report the rest.
## Tribe members frame by frame: coop.track in Tribe and tribe_walk (2026-11-01, fix/w30-tribe-walk)
<a id="testing-tribe-walk"></a>

* `coop.track` in Tribe (BUG-0365): one row per member of the player tribe per frame, `who` = its
  owner and the member id in the last column. Column order of `coop.track dump` (stable; new columns go
  at the end, parsers take `len >= ` the columns they know): t who flags tp px py pz dx dy dz tx ty tz
  gv rv anim gait gait2 moving speed_state vis sx sy (23), hd ht (24-25, BUG-0366: heights over this
  game's ground), id (26, BUG-0365: the Tribe member id, 0 in Creature). On the host its own game's members (the target = its position, `rv` = its game
  speed); on a guest the mirrors: the drawn position (before the mod's driving), the target the
  driving aims at this frame and `rv` = the speed of that host state. The ring holds 60 000 rows
  (~2 min with 6 members at 70 fps).
* `coop_tribe_walk.py` (suite `tribe_walk`, ~2.5 min cold; `tribe_walk_mobile` nightly: SPORE_NET=mobile,
  both games at 30 fps): the host spawns 3 members (3 each), a player walks its members ~30 s by
  box-select (one or the group) and right-clicks on the ground 220-420 px off, sometimes a new order
  before they arrive; the other keeps them in view with the minimap. Both directions; the guest's own
  members are mirrors too (`guest sees its own members`). Checks per view of mirrors: no frame step over
  3x the top speed x max(this frame, the frame before) (min 0.75 m); 0.1 s windows standing <= 5 % and
  at 0.6-1.4x the speed of the target's path >= 85 %; in the frames whose target moves at 1-5 m/s
  (starts and ends of walks) the drawn step off the target's step of the frame before (what a frame
  draws was placed in the frame before) by more than half of it + 1 m/s x the frame time in <= 10 %
  (0.7.3.3: 14-29 %, the fix 1-5 %, the host's own game 0-5 %); walk gait >= 80 % of the frames on
  screen. Metrics tried and dropped: per-frame speed against the state's reported speed or a +-0.1 s
  path (the host's own members start and stop within one frame, and the frame pacing makes single
  frame speeds noisy: both flagged the host's own game in up to 24 %). The
  host's own game is printed as the reference. `--fps N` caps both games,
  `--dir host|guest`, `--seed`. `SPORE_FIX_OFF=tribe.mirror_walk,tribe.mirror_time` fails.
* A loaded machine (lab load 31 on 12 cores, 2026-11-01): the observer's frames jitter 25-120 ms and
  5-19 % are over 0.1 s; the steps then say nothing about the driving. The test leaves out the steps
  within 0.2 s after a frame over 0.1 s and, when more than 3 % of the observer's frames are over 0.1 s,
  prints `NOT MEASURED` and skips the window and stop-and-go checks for that view (calm runs: 0-2.2 %,
  also the failing runs of the 0.7.3.3 driving: 0.1-1.6 %). Under 40 frames at 1-5 m/s (a few walks at
  30 fps) the stop-and-go check is skipped the same way (the 0.7.3.3 driving's failing runs: 98-1116).
  Steps within 0.2 s after a jump of the target itself (faster than 1.5x the top speed: over a mobile
  network a late or lost state corrects the extrapolated path by 0.3-0.8 m in a frame) are left out
  too: the driving smooths such a jump on purpose, and the "step against the target's step" check
  flagged that smoothing (10 % in a passing run; the 0.7.3.3 driving's runs still fail with it).
* Pitfalls: the lab's games ran at ~70 fps (14-15 ms frames), not 30; the minimap's camera jumps give
  100-230 ms frames, and the next frame draws the way made up (allow the frame before's time); a click
  on the ground near a tree or a member gives no walk (the test logs `nothing`).
## Log gates: what the games' own logs say fails a test (2026-11-01, feat/w30-lab-gates)
<a id="testing-log-gates"></a>

Up to 0.7.3.3 the lab read only the test's own output (`faults:` lines, `RESULT:`): a passed test
could hold `sync-check: MISMATCH` lines, hitches and address space trouble in its game logs, and
186 passed results did. `tools/lab.py run_one` now judges the lines the lane's games wrote during
the test (`tools/loggates.py`):

* **Which lines**: at the test's start the lab takes each game log's size and first line
  (`loggates.log_marks`, stored as `log_marks` in results.json); afterwards only what follows that
  offset counts, so a warm game's log with an earlier test's lines is no problem. A game that
  started again during the test (another first line) wrote a new log: all of it counts. Extra
  guests' logs (instances 20+N / 30+N) count when written since the start.
* **sync-check**: a `sync-check: MISMATCH` line (the mod's own digest compare found a difference
  that lasted 3 s; docs/REPORTING.md). Tests that force one on purpose print
  `LAB-MARK sync-force on|off <time>` around it (`twoinst.lab_mark`, used by coop_diag.py): the
  gate leaves out MISMATCH lines from 1 s before `on` to 15 s after `off`.
* **hitch**: `perf: hitch N ms (<activity>)` with N over 2000 while a stage is played: not when the
  activity says loading, saving, receiving, editor, cinematic, vote or nothing controlled, not in
  the galaxy menu, not within 20 s of a `stage:` / `world: loading` / boot / "game saved box" line,
  and not 2000-2039 ms (SporeKit's frame source switch: Present lost for kPresentLostSec = 2 s,
  frames come from the game loop; 54 of the 66 in-play hitches over 2 s in the recorded logs were
  exactly this step).
* **mem-low**: `mem: the game's address space runs low` (diag_stats.cpp: under 300 MB free or a
  largest block under 48 MB).
* **as-slope**: the free address space of the per-minute `perf: fps avg ...` lines, least squares
  per stay in one stage of one process (a new segment at every `stage:` line / load / boot; the
  first sample of a segment and samples within 90 s of its start are the load's own growth; at
  least 4 samples over 3 minutes): a loss over 25 MB/min fails. Recorded passed runs: 99 % under
  6 MB/min, at most 7 (campaign games); BUG-0307's leak was ~350 MB/min.
* **monitor**: `MONITOR-VIOLATION` lines of the invariant monitor (#testing-monitor).
* Per test in suite.py: `gates={'hitch_ms': 5000}` (a threshold of `loggates.DEFAULTS`),
  `gates={'off': ('hitch',)}`, `gates=NO_GAME` (unit, win_unit: the lane's idle games are not
  theirs); `allow=[allow('sync-check', 'BUG-NNNN', 'reason', stage='Tribe', kind='p apart')]`
  (also `who=`, `match=` a regex; gate `monitor` with `kind=<invariant>`); `ALLOW_ALL` for every
  test. An allowed finding is printed as `allowed ... [BUG-NNNN: reason]` in the lab's output and
  in summary.md's "Log gates" section, never silent; gates_selftest.py checks that every allow names
  an existing ticket and a reason.
* `SPORE_GATES=report lab.py run ...`: the gates only report (`report only` lines), `off`: none.
* `lab.py gates` (= `tools/loggates.py report`): the gates over every recorded result of the main
  checkout and the worktrees, report only. Recorded results have no offsets: their window is the
  test log's last write minus the test's seconds, and the game clock is matched to the lab's in
  half-hour steps (Wine's clock was an hour ahead of the lab's on 2026-10-29: last line 23:01,
  copy 22:01).

First report (2026-11-01, 3980 results with game logs, 3438 passed): 185 passed results would
have turned red, 182 of them on sync-check, 3 on hitches (3.4-4.0 s in Civilization,
campaign_space), none on mem-low or as-slope. Red in most of their runs: campaign_space 45/45,
campaign 21/24, npc_far 15/15 (Creature `p0 10.7` apart on the guest: the host's puppet ~10.7 m
from the host), npc_pooled 9/11 (the same), cell_pause 6/6 (Cell `c missing`), space_finale_wait
4/4 (Space `p0 20.7` apart), guest_editor_hold 3/3, cell_far_guest 8/14, campaign_tribe 20/50,
campaign_short 18/74; rarely: chaos_tribe 4/89, tribe_coop 1/90 (Tribe `p apart`), cell_particles
1/87. diag_cell / diag_creature were the forced mismatch (now cut out by LAB-MARK). The kinds per
stage: Civilization `v missing` (35 results), Cell `c missing` (31), Tribe `p apart` (31),
Civilization `p apart` (29), Creature `p apart` (27), Space `p apart` (25), Space `p missing` /
`p extra` (12), Tribe `t missing` (10), Creature `n missing` (9), Tribe `n apart` (8), Cell
`cell food total` (5), Space `s missing` (4). Each needs a fix or an allow entry with a ticket
before the suites that hold it are green with the gates on.

## The invariant monitor: checks while a test plays (2026-11-01, feat/w30-lab-gates)
<a id="testing-monitor"></a>

`tools/tests/monitor.py`: `twoinst.watch()` (every `twoinst.start`) starts a thread over the
test's games, `coop_multi.start_extra` adds the extra guests (party, campaign). Every 5 s it reads
the new lines of each game log (the mod's `trace:` lines, every 10 s per machine, with the shared
session clock `[s T]`) and evaluates:

| invariant | violation (each needs its state on several lines in a row) |
|---|---|
| together | two machines' own trace lines say the same stage and world (both playing: avatar, no vote, in world) for 20 s, but one's partner line says `same_place=0` |
| puppet | a partner line with `same_place=1`, alive and in a world, `puppet=hidden: not spawned (yet)` or `state older than 5 s` on 2 lines |
| link | clean network only (no SPORE_NET): resends / sent between two lines over 0.5 (at least 50 sent), `pending` over 400 or a state 5 s old in the same place, 3 lines in a row |
| totals | the shared totals of the own lines (dna in Creature, the stage's `food=` / `money=` ...; `(unsynced)` values left out) of a guest outside every value the host had within 6 s, beyond 1 point or 2 %, on 3 comparisons spanning 20 s |
| hp | a partner line's `hp=a/b` (the state received) differs from that player's own line (max hp, or hp by more than 1 or 10 %) while the player's own hp is steady for 12 s, 2 lines |
| objects | `tiles: ... cell pool N/M` over 90 %; with `monitor={'census_period': 60}` also `coop.census` counts over `OBJECT_LIMITS` (c 4000, v 800) or over 3x the first count + 200 |
| stage_apart | a guest plays another stage / world than the host (both playing, the host not away; not two star systems in Space) for 90 s |

A violation is printed once per episode as `MONITOR-VIOLATION <invariant> <who>: <text>`;
`Checks.result()` reads the logs once more first. The lab fails the test on these lines (log gate
`monitor`, allowed per test by `allow('monitor', ticket, reason, kind='hp')`). `SPORE_MONITOR=off`
or a JSON dict of `monitor.DEFAULTS` overrides (suite.py `monitor=`); `SPORE_MONITOR_SKIP=hp,link`.

Calibration over the recorded passed results (`monitor.py report`, 2026-11-01): campaign_space
totals 42 (Civilization `money`: the host 10960, both guests 10590 for 20+ s, beyond the 2 % that
party.check_res allows), stage_apart 11 (a guest still in Civilization 90 s after the host entered
Space), link 3; campaign link 2, totals 2; cell_rejoin_leave link 4 (pending 600 after rejoins);
single ones in campaign_short, nest_species (puppet: state older than 5 s), chaos_tribe, ui_status,
chaos_creature_long. Every other test: none.

Hooks (inactive until the mod writes their field; `monitor.NEEDS` has the exact text). New fields
go at the END of a trace line (after `pending=` / `editor_pending=`) so today's parsers keep
working. Update 2026-11-01 (feat/w32-monitor-diag): the mod writes all of them now and the rules are
active, see [testing-monitor-fields](#testing-monitor-fields); the list below is the original plan:

* drawn (audit M1/M2): partner line `drawn=(x y z)`, the puppet's position read back from the game
  object (not the interpolation target); compared with the partner's own `pos=` at the same session
  time (3 m Creature / Tribe, 6 m Civ, 12 m Space). coop.track has drawn positions per frame for
  Creature puppets, but timed from the track start on the local clock: session-clock rows would
  also allow a per-frame jump check.
* ground (M3): own and partner lines `agl=<m>`, height above the terrain under the avatar /
  puppet; violation below -0.5 m or above 3 m while not jumping, flying, swimming or in Space.
* puppet_hp (M4): partner line `puppet_hp=<hp>/<max>` read from the puppet creature itself (what
  the health bar shows; modes.cpp pins puppet health to its max today).
* posse_damage (M8): a log line on the hit player's game `coop: damage: <player id> hit by <kind>
  owner=<host|guest id|npc> posse=<0|1> amount=<hp>`; violation: damage from a posse / pack / ally
  member (npc.cpp forwards any non-avatar, non-puppet hit on a guest puppet as MSG_DAMAGE).
* unlocks (M15): own line `parts=<count>:<hash>` of the unlocked creature parts; violation: a
  guest's count under the host's 60 s after an unlock in a shared world.

## Degraded conditions: frame caps, a network per guest, bursts (2026-11-01, feat/w30-lab-gates)
<a id="testing-degraded"></a>

* `SPORE_FPS=host:30,guest:15,extra:20` (or one number for every game; `extra1` / `extra2` one
  extra guest): `twoinst.start` and `coop_multi.start_extra` cap the frame time with `frame.limit
  <1000/N ms>`. Such games are not reused by the next warm test (lane_state `fps`).
* `SPORE_NET` (tools/tests/netem.py) now gives every guest its own UDP proxy and port (lane N:
  Bob 47900+N, Carol / extra1 47920+N, Dave / extra2 47930+N; `coop_multi.join_cmd`): a proxy
  knows one guest address, so 3-4 player parties over SPORE_NET sent every extra guest's packets
  to the host from Bob's proxy before. Each proxy has its own random stream.
* New keys: `burst_every=S,burst_s=S,burst_loss=F` (Gilbert-Elliott in time: good state S s on
  average, exponential, then a burst of burst_s s losing F of the packets), `spike_every=S,spike_s=S,
  spike_ms=MS` (latency spikes every ~S s +-30 %), `step=AT:MS[/AT:MS]` (the base delay from AT s
  after the start), `rate=KBIT,queue=MS` (a bandwidth cap per direction; a packet that would wait
  over MS ms, default 300, is dropped). A misspelt key is refused (it was silently ignored).
* Presets from the players' logs (rtt 70-150 ms, spikes to 400+ ms, bursts of 10-30 % loss):
  `players` (delay 50 +- 25 ms one way: median rtt ~100 ms, 99.5 % ~540 ms; bursts of 20 % for
  ~4 s every ~45 s; spikes +200 ms for 3 s every ~60 s), `players_bad` (rtt ~150 ms, 30 % bursts
  every ~25 s, spikes every ~30 s, 2 Mbit/s), `burst` (rtt 70 ms and 30 % bursts every ~20 s),
  `lowband` (512 kbit/s), `step` (rtt 70 ms, 400 ms from 30 s to 60 s). The old `dsl` / `mobile` /
  `awful` are unchanged.
* `netem.Model` decides each packet with its own clock: gates_selftest.py checks the loss,
  burst, spike, step, rate and duplicate statistics offline and two guests through two proxies.

## Coverage, fix switch claims and the players suite (2026-11-01, feat/w30-lab-gates)
<a id="testing-coverage"></a>

* `lab.py run players`: generated variants (suite.py `VARIANTS`, `VARIANT_TESTS`, `FOUR_P`) of
  creature, team, tribe_coop, cell_coop, civ_coop, space_coop, puppet_walk, npc_run_hp, sync,
  select, party_creature4, party_tribe4, party_space4 as `<test>_fps15` (guest at 15 fps),
  `_mobile`, `_burst`, `_fps15burst`, `_players` (the players preset), plus party_cell4,
  party_civ4, multi4, npc_multi4 (four players) and their `_players` runs. 73 tests, cold, not in
  full / nightly. The older hand-made `<test>_mobile` nightly tests joined the suite.
* `lab.py coverage [--days 7]`: every suite test with its last recorded run (main checkout and
  every worktree's out/lab), its result, how often it ran and passed; `NEVER RUN` first. On
  2026-11-01: 83 never ran, 11 outside the new players suite: chaos_cell_long, chaos_civ_long,
  chaos_space_long, chaos_tribe_long, chaos_tribe_awful, legless_guest_dontsave, play_civ,
  play_space, play_tribe, rejoin_awful, rejoin_race_long.
* `lab.py fixcheck`: every "SPORE_FIX_OFF=x fails" claim of suite.py (on an add() line or in the
  comment right above it) against the recorded runs of that test whose output switched x off
  (`SPORE_FIX_OFF: x off`): confirmed by a FAIL run, CONTRADICTED when it only passed, unverified
  without a run; exit 1 on a contradiction. 2026-11-01: 47 confirmed, 1 unverified (update_leak,
  core.update_unwind), and leave_avatar's world.leave_avatar claim was contradicted (5 PASS runs,
  lab 20261029-085208 / 085452 / 085805-fix-w6-avatar; BUG-0264 records no-fail-run): the comment
  now says so.

## Release rule: what must have run on the release commit (2026-11-01, feat/w30-lab-gates)
<a id="testing-release-rule"></a>

Before a tag `vX.Y.Z` (CLAUDE.md 4):

1. On the release commit itself, with the log gates on (`SPORE_GATES` unset): `smoke`, `full` and
   `multi` passed (allowed findings are fine, each has its ticket). Check:
   `tools/lab.py coverage --commit <commit> --require smoke,full,multi` (exit 0).
2. Since the previous release, on a commit with the release's mod code (no change to mods/ or sdk/
   after it): `nightly`, `extended`, `players` and one whole `campaign` (or `campaign_short` three
   times). `lab.py coverage` shows no `NEVER RUN` and nothing "not run for" more than the cycle in
   these suites; a test that cannot run (a lane, a fixture) is named in the release notes.
3. `tools/lab.py fixcheck` has no CONTRADICTED claim.
4. `make test` passed (it runs lab_selftest.py and gates_selftest.py).
## Creature health between players: coop.hp, partner_hp, party, brain_hp (2026-11-01, fix/w30-creature-hp)
<a id="testing-creature-hp"></a>

* `coop.hp` (main thread): `own: hp=<health>/<GetMaxHitPoints> level=<brain level> species=
  player_species= same|other base_game=<the game's BaseMaxHitPoints without the fix>`, then
  `puppet <id>: hp=<game health>/<game max> reported=<state>`: what each game shows, not mod counters.
  `coop.hp pack` lists the host's pack members with `from_partner=` (AI stimuli naming a partner's
  puppet), `coop.hp attack <player>` sends the host avatar's attack message on a puppet.
* `creature_partner_hp` (~45 s cold, warm-capable): `coop.team hit` is a real TakeDamage on the own
  avatar; the pack regen (2 %/s of max next to the partner) moves health between the two reads, the
  test allows 8 % of max.
* `creature_party` (~45 s warm): `coop.npc recruit` + `gonpc 3` + the guest's `gopos` puts all three
  together; the recruit walks to the host on its own only sometimes. While a brain-upgrade cinematic
  plays (`coop.status`: `cinematic F6A4B1E1 playing`) hits on the host's NPCs land with no health
  change: close the captions (Enter on both) first.
* `creature_brain_hp` (~65 s, cold): 250 DNA on the host (`AddEvolutionPoints f:250`) gives level 1 on
  both games through the DNA sync; Enter closes CRG_BrainUpgrade1 on each. The guest plays its own
  creature in the fixture (creations-guest: Kirgle, species health 6).
## Creature: the guest's own editor's locked parts and a guest's view of pack paints (2026-11-01, fix/w30-progress)
<a id="testing-own-editor-locks"></a>

* `coop_guest_editor_locks.py` (suite `guest_editor_locks`, ~70 s, BUG-0368): the host's
  `coop.vote editor`, the guest's ready, then on the guest `coop.editor`: the open editor (the editor
  game mode from the mode manager's list, id 0xDBDBA1) and its request: `items` (+0x94) must be the
  guest's collected parts and the palette's rule (no items = available) must leave parts locked
  (258 of 312 on the creature fixture; 0 with the fix off). Screenshots `guest_editor_locks.png`
  (locked silhouettes) and `host_editor_locks.png` (the test request of `coop.vote editor` has no
  items: the host's test editor offers every part, the real host's editor is the game's own).
  FAIL: `SPORE_FIX_OFF=creations.own_editor_items`.
* `coop_guest_pack_paints.py` (suite `guest_pack_paints`, ~55 s, BUG-0369): one lane, the lane's
  `bp1content/BoosterPack_01.package` moved aside for the whole test (both games without the pack; the
  lane state is marked so the next warm test starts new games); the host plays Tiger Horse with the
  pack's Dottie base and coat effects (header bytes 26..33 of its model). Checked on the guest:
  `coop.creations paints 290229B9` cc=0 with the base substitute 8C077662, the species built, no
  fault; screenshot `guest_sees_host_paints.png` (fix off: the host's creature is black).
  FAIL: `SPORE_FIX_OFF=creations.paint_substitute` (cc=2).
* `tools/crash_triage.py` (BUG-0372): the coop version comes from the crash file's Environment
  (`mods: coop X`) or coop section, never from the Summary's "latest co-op summary" of an earlier
  run; `tools_selftest.py triage` case (g).
## The puppet's height, a weak guest PC and a home connection (2026-11-01, fix/w30-puppet-jump)
<a id="testing-puppet-ground"></a>

* `coop.track` rows end with `hd ht`: the drawn puppet's and the target's height over this game's
  ground (25 columns; `coop_puppet_walk.dump` reads 23 or 25). `coop.track lift <m>` (tests) takes the
  partners' Creature states <m> further from the planet's centre in this game (negative: under the
  ground). `coop.abilities [play <i> | lift <m> | goto <x> <y> <z>]` (tests): the avatar's abilities,
  use one, put it <m> higher (it falls back: half a jump), put it on the ground elsewhere.
* netem (`SPORE_NET`): preset `player` (the 2026-11-01 report: rtt 70-150 ms, spikes, 10-30 % loss
  bursts), written with the shared spike / burst keys of docs/06-testing.md#testing-degraded (the
  branch's own `spike=`/`burst=` keys were merged into those).
* `coop_puppet_walk.py` options: `--guest-fps N` (frame.limit on the guest), `--goto x,y,z` (both
  players to that ground first; `-195,-465,-45` is a slope 0.3-0.5 of the creature fixture, the hill
  at `-205,-447,-118` is rock), `--stand S` (the walker stands after its walk), `--jumps P` (hops by
  `coop.abilities lift 1.5`), `--ground` (checks: the puppet starts the fall animation 0x0692E6A9 at
  most once more than the walker's own creature, and is never drawn 0.3 m under the observer's ground).
* Suites `puppet_slope` (guest 30 fps, `SPORE_NET=player`, the slope) and `puppet_under`
  (`coop.track lift -1`): `SPORE_FIX_OFF=creature.puppet_ground` fails both.
* Pitfalls: the falls happen walking downhill only (flat fixture ground: 0-2 in 40 s even with the
  bug); a run on rock counts nothing (+0x137 is 0 on rock all the time, GetHeightAt is the terrain);
  the spawn moves after a `--goto`, so these tests are cold. Exploration with the fix off on the slope
  gave 4-7 falls per 40 s, so one FAIL run is not guaranteed every time (the check allows 1).

## Walks far from the start stay on land; shared DNA notices (2026-11-01, fix/w31-creature-gate)
<a id="testing-walk-on-land"></a>

* `coop_npc_pooled.land_path(con, a, metres)`: of 16 directions the one whose lowest ground along the
  walk is highest (`coop.abilities ground x y z`: the terrain radius there, no move) and its height over
  the water (`SEA` = 499.2 m on the creature fixture); `walk_to(con, a, b, t)` puts the avatar onto the
  ground (`coop.abilities goto`); `drawn_apart(host, guest)`: how far the guest's game draws the host's
  puppet from the host's own avatar (coop.track column d, after the game's update, not the mod's write).
* Pitfall (BUG-0377): `coop.npc gopos` at a fixed radius (510 m) puts a creature in the air or under the
  ground; over the sea the deep water caption cinematic 5E205931 freezes the host's avatar there and the
  guest's game draws the puppet on the water: sync-check `p0 10.7`. npc_far / npc_pooled walked into the
  sea until 2026-11-01.
* `old_species` (~60 s, cold, BUG-0370): the guest switches its creature (`coop.creations next
  creature`), the host spawns a creature of the old key (`coop.npc spawn`, with `coop.fix npc.strays off`:
  the stray pass removes it within a second otherwise); the guest's census must show a proxy of that
  species. Marks the lane state so the next test starts cold. FAIL: `SPORE_FIX_OFF=creations.old_species`.

<a id="testing-dna-causes"></a>

* `dna_notice` (~60 s, cold): the guest kills a host NPC (`coop.npc hit <id> 100000`), the host one
  (`coop.npc hosthit`); the notification panels read back from `coop.status` (`toast <kind> (...)
  '<text>'`; a text may hold an apostrophe), the `dna: a -> b cause=...` lines of both game logs (read
  from the byte size before the test). `dna_notice_cell` (`--cell`, ~90 s): `coop.pool food N` on each
  side; the meal notices are gathered 15 s, the test waits up to 22 s. A feature: no FAIL twin.
## Setup folder checks and the GitHub API limit (2026-11-01, fix/w31-installer)
<a id="testing-installer-w31"></a>

* `win_unit` (tests/win_tests.cpp) has four more tests:
  * `test_ga_folder` (BUG-0391): a Steam-like Spore folder with `SporeBin` and no `SporebinEP1`:
    `inst::game_root_of` steps up from `SporeBin`, `inst::folder_problem` / `do_install` say
    "Galactic Adventures is not installed", `inst::steam_libraries` reads both formats of
    `libraryfolders.vdf`, `inst::pick_game` returns that folder when no game is found.
  * `test_setup_ga_window` (BUG-0391): the real `SporeCoopSetup.exe` (hidden window) with
    `/dir:<spore>\SporeBin`; the test reads every child window's text with `WM_GETTEXT`
    (`SendMessageTimeoutW`, works across processes) and checks the folder field and the "Game:" line.
  * `test_web_fallback` (BUG-0392): an in-process HTTP server (Winsock, port from `bind(0)`) plays
    api.github.com and github.com: `/repos/<o>/<r>/releases/latest` answers 403 or 429,
    `/<o>/<r>/releases/latest` answers 302 to `.../releases/tag/v<x>` (absolute or relative
    Location), the assets are under `.../releases/download/v<x>/`. Checked: the release is found,
    a pack that does not match `SHA256SUMS.txt` or is of another version is refused, the page is not
    asked while the API works, `do_install` installs, and with both refusing the message names
    `install.bat`.
  * `test_setup_api403` (BUG-0392): the real setup in silent mode (`/S /api:<fake API>`) installs.
* Switches: `win_unit.py --old-ga` (setup-only `inst::g_ga_fix`, the setup's `/oldga`) and
  `--old-web` (`update.web_fallback`, the setup's `/oldweb`); in the lab `SPORE_FIX_OFF=update.web_fallback`
  and `SPORE_WIN_UNIT_ARGS=--old-ga` do the same. `win_unit.py --setup <exe>` runs the setup tests
  against another `SporeCoopSetup.exe` (under the repository, e.g. `build/main-setup/`: one built
  from `git archive main` fails `test_setup_ga_window` and `test_setup_api403`).
* `update_check.py --api403` (suite `update`, in game, ~4 min): the local server's API path answers
  403; the game must offer and install the next version through the release page (`coop.update`
  shows `via=page`). `SPORE_FIX_OFF=update.web_fallback` switches the fix off after the start's check
  and runs `coop.update check` again: FAIL ("the game finds SporeCoop ..." not met).
* Wine's WinHTTP and the redirect: `request(..., follow=false)` sets `WINHTTP_DISABLE_REDIRECTS`
  and the `NEVER` redirect policy and reads `Location`; when a 200 comes back anyway it takes
  `WINHTTP_OPTION_URL` (where the request ended), so both behaviours give the tag. Wine (lane 4's
  bottle, 2026-11-01) honours the switch: `test_web_fallback` prints "the redirect was not followed"
  (the fake tag page is never asked).
* Seen 2026-11-01 on this machine: api.github.com answered 403 ("API rate limit exceeded", remaining
  0) to the setup; the setup built from main failed with the players' message, the fixed one
  installed 0.7.3.3 from github.com through the release page (`/S` into a fake game folder in lane 4's
  bottle).
## Space: what each game draws of the other's ships (2026-11-01, fix/w31-space-gate)
<a id="testing-space-drawn"></a>

* **Where the sync-check reads**: the digests are built in `g_sess.update()`, the first thing of the
  mod's frame, before any puppet or mirror is driven: they hold what the game's own update made of
  the last write, i.e. what the frame showed (docs/re/space.md#space-pinned-ship-drawn). `coop.sync`
  (console) reads after the write: our own target, not what is drawn.
* `coop.fleet track on|off|dump [from] [n]` (squadron.cpp): every ship each frame at the top of
  `on_frame` on the session clock, `t kind id x y z ox oy oz zoom` (m own ship, p puppet, s NPC
  ship / mirror; ox..oz the ship's mOffsetFromPosition, zoom its mZoomAltitude). The console answer
  is cut at ~32 KB: dump at most ~400 rows per call. Rows come at the game's frame rate (~15/s with
  two lab games on a lane).
* `coop_space_drawn.py [--planet]` (suite `space_drawn`, `space_drawn_planet`): the host, then the
  guest flies with real input (clicks into the view, WASD, on the planet the wheel); per kind the
  drawn rows against the owner's own rows: `now` (same session time, lag included) and `path` (the
  closest point of the owner's path over the 0.5 s before: lag taken out). Checks path p90 <= 3 m and
  `now` <= 0.5 s x speed + 3 m in 90 %. Measured 2026-11-01 (lab 20261101-093823, -094339): in the
  start system and on the planet puppets path p90 0.07-2.8 m with and without the pin fixes (the
  offset is 0 there), mirrors 0-2.5 m; the ships flew 3-11 m/s (clicks and keys move a ship slowly;
  NPC ships up to 123 m/s). It does not reproduce BUG-0378 (that needs the galactic core).
* `coop_space_finale_wait.py` now also tracks both games for 6 s in the core's system after the
  host entered: `core: the host ship on the guest` checked (BUG-0378: SPORE_FIX_OFF=space.puppet_pin
  draws it 21.0 m off, median, lab 20261101-095432; with the fix 0.0 m, lab 20261101-100032), the
  other direction printed only (the guest orbits at ~200 m/s there; BUG-0388).
* Lag at Space speeds (observed, lab 20261101-095432): the guest's ship orbiting at 207-235 m/s was
  drawn on the host 28.6 m (median) behind its own position at the same session time, 0.03 m from its
  path: lag of ~0.13 s, not a desync. The sync-check's fixed 10 m for players flagged such ships
  (`p1 29-31`, campaign planets 10-30 m at 60-100 m/s by the trace positions); `sync.speed_slack`
  allows speed x (0.2 s + the guest's round trip [+ half the owner's]), capped at 1 s / 300 m.
* `s missing` / `p0 missing, p<guest> extra` (the gate report's 12 + 4 Space results) were in world 0 in every recorded host log but two (`s1` missing in space_mirror_model, made on purpose: a host ship whose model the guest lacks gets no mirror, BUG-0323) (the galaxy view,
  where no puppet is shown and no ship mirrored): `sync.no_place` compares no players or ships there.
* CORRECTION (2026-11-01, fix/w31-installer): the first BUG-0391 fix took a Steam `common\Spore`
  without `SporebinEP1` for "Galactic Adventures is not installed". On Steam GA is its own app
  (24720) in its own folder `steamapps\common\spore 24720` (appmanifest_24720.acf "installdir";
  seen on this machine's Steam, `SporebinEP1\SporeApp.exe` 25 066 744 bytes), base Spore (17390) in
  `common\Spore` never has `SporebinEP1`. More win_unit tests: `test_steam_ga_folder`
  (`inst::resolve_root`: sibling `Spore 24720` / the appmanifest's installdir / another library),
  `test_setup_steam_ga` (the real setup's window and `/S` with `/dir:<lib>\steamapps\common\Spore\SporeBin`
  install into `spore 24720`), `test_real_steam` (read only: `win_unit.py` passes
  `--steam Z:\home\...\.local\share\Steam` when that Steam exists; Bottles' Wine sees it under Z:).
  Switch `inst::g_ga_dir_fix` (setup `/oldgadir`, `win_tests --old-gadir`, lab
  `SPORE_WIN_UNIT_ARGS=--old-gadir`). The setup built from c891445 (`--setup build/main-setup/...`)
  fails `test_setup_steam_ga` with "Galactic Adventures is not installed".
## Civ cities, the Civ pointer, money totals and the planet wait in the gates (2026-11-01, fix/w31-civ-gate)
<a id="testing-civ-gates"></a>

* `coop_civ_city_burst.py` (suite `civ_city_burst`, ~60 s warm, BUG-0380): the host founds 3 NPC
  cities with `SpawnNextCity(ex, 1, 0)` (instant: not broadcast, the campaign's shortcut); the
  guest's own game must have every host city (60 m) and every host vehicle id bound to a game
  object within 20 s, and no extra city 15 s later. `SPORE_FIX_OFF=civ.city_burst` fails (the old
  catch-up needs 6 + 15 s per city).
* `coop_civ_cursor.py` (suite `civ_cursor`, ~65 s warm, BUG-0380): the host's mouse goes round a
  circle (`move x y` every ~0.1 s) while `coop.diag sync now` runs; the guest's copy of the host's
  pointer trails 45 m median (coop.sync p0 on both), a pointer at rest matches to 0.0 m. Holding W
  in Civ moved the pointer only for a moment (camera pan stops): use the mouse.
  `SPORE_FIX_OFF=diag.rts_cursor` fails (`MISMATCH ... p: 1 apart (p0 74.9)`).
* `coop_space_share_wait.py` (suite `space_share_wait`, cold, BUG-0382): the host lands, the guest
  rejoins; the guest's log must show the host's notice and the host's trace `share_wait=planet`
  within 25 s; after the takeoff the guest loads the host's system. `SPORE_FIX_OFF=
  world.share_wait_notice` fails.
* coop_campaign.py's Civ shortcut founds the remaining cities with `SpawnNextCity(ex, 0, 0)` now (the
  game's own call, broadcast to the guests); `(ex, 1, 0)` was not broadcast and left every guest to
  the catch-up (campaign_space: Civ v missing in 45 of 45 runs). Lab 20261101-110949: no Civ
  MISMATCH, no monitor violation; the run stays red on Space `p apart` (BUG-0378).
* `diag.rejoin_grace` (BUG-0380): the host's sync-check dropped no entry of a player who left, so
  a guest that left and joined again (civ_city_join) was checked on its old schedule, 6 s after
  its world load; now a returning player is first checked 10 s after the join, like a newcomer.
* Monitor (tools/tests/monitor.py): `totals` now always includes the host's trace lines right before
  and right after the guest's (a Civ treasury grows ~400 per 10 s: the +-6 s window often held one
  host line; the 42 campaign_space violations were all such samples, none in a replay now);
  `stage_apart` is no violation while the host's own line says `share_wait=planet|cinematic`.
  gates_selftest.py has both cases (they fail on the old monitor).
## Cell sync-check findings of the log gates: what was a desync (2026-11-01, fix/w31-cell-gate)
<a id="testing-cell-sync-check"></a>

BUG-0379: the first `lab.py gates` report had Cell `c missing` in 31 passed results and `cell food
total` in 5. Per test, with the evidence:

* **cell_pause** (6/6): the test removes the guest's mirrors 12 times (`coop.pool zap 5000`) while
  its pause menu is open; BUG-0348's fix then holds new cells (the broadphase's pending list is
  near full), so the guest really has none of the host's cells until it resumes (`c: 706 missing`,
  `2 objects compared` = the two players only). A difference the test makes, not one a player gets
  (without the churn a paused guest creates a few cells). The rounds are now marked `LAB-MARK
  sync-force on|off` and after the resume the host's own sync-check must count an OK (no MISMATCH):
  `ok 3. after the resume the host's sync-check finds the guest equal`. cell_particles (1/87, the same
  `2 objects compared`) and cell_churn remove the mirrors the same way and are marked too.
* **cell_far_guest** (8/14), **guest_editor_hold** (3/3), **cell_leave** (3/23): every red result
  is from 2026-10-30 runs of fix/w9-* branches, before the cell budget and partner fixes (BUG-0295,
  BUG-0298). guest_editor_hold's: the test's `coop.pool food 1000` jumps several levels; the guest
  had 7 mirrors at s 17.7 and 128 at s 27.7 (the new level's pool still coming). Not seen on 15539ad.
* **cell_sap** (3/31): all three in one run that failed with 1332 caught faults (the BUG-0337 work
  before its fix): a broken game, not the check.
* **campaign_short** (12 `c missing`): the recent one (20261031-181517 #3, Dave: `904 objects
  compared ... c: 14 missing`): four players, the guest held exactly the 900 cells it was sent, the
  check wanted the 900 nearest to the guest's own position, the host had picked them around its
  view centre of the guest (a state late): the cells near rank 900 differ. Fixed in the check
  (`diag.cell_margin`: only the nearest 80 % of the 900 must be there). The 2026-10-29/30 ones
  (`1098 objects compared`, 447 missing) were before the 900 cap. Not run again (four players, long).
* **cell food total** (5: campaign, campaign_short, cell_new_game): the host sends its total every
  0.5 s (MSG_CELL_FOOD) and builds its digest a round trip after the guest's; with food coming in
  steadily both checks 3 s apart caught the guest one meal behind, a different meal each time
  (campaign_short 20261031-103900 #3: host 240 at s 176.9 and 252 at s 179.95, Carol 240 at s
  177.3 and 252 at s 180.4, the MISMATCH at s 180.26). Fixed in the check (`diag.total_lag`: a guest
  value the host had within 3 s before asking counts as equal); both games' totals were equal.

Test `cell_sync_check` (`coop_cell_sync_check.py`, ~2.5 min): B: food 300 crowds the pool (the
host sends the guest 900 of more, `send_capped`), the guest swims to and fro, the host asks for a
check every 5 s; A: the host eats a point every 0.3 s for 20 s. Each phase: no new MISMATCH, at least
3 checks; afterwards the guest holds the host's cells and the same food total. How crowded the
pool gets varies (the send was capped in 0 to 9 rounds of 5 runs): printed, not checked. `SPORE_FIX_OFF=diag.total_lag`
fails A (lab 20261101-095302, 20261101-100218: `cell food: host 340.0, guest 338.0`).
`diag.cell_margin` off did not fail in 2 players (lab 20261101-095952: the host capped the send in
only 3-9 rounds; the boundary needs a pool crowded for long, as with four players): its check is
the unit test in net_tests (test_diag_compare) and the campaign log above.

Test `cell_ko_food` (`coop_cell_ko_food.py`, ~1.5 min, BUG-0364): the guest (30 m from the host,
nobody revives it) and then the host are knocked out; while out, the game's eat event for the 2-4
nearest plants on its screen (`coop.pool eat <key>`, EatFood); the food totals of both games must
not grow by them. `SPORE_FIX_OFF=cell.ko_no_eat` fails (+12 per plant on both games). Pitfalls: a
`coop.pool tp` right after the start is undone by the hatch (the test moves again until 15 m away);
the plants it touched drift off the screen or are eaten by the pool (printed only).

cell_coop pitfalls found on the way (fix/w31-cell-gate): the `revives=` / `respawns=` counters of
`coop.pool` belong to the game process and survive the warm reset, so the test compares them with
their values at its start (BUG-0396: every warm `--repeat` run failed before); the host's idle cell
can eat a plant in the 2 s of the shared food check, which now allows the host's own `own_points`
(BUG-0395, 1 in 130 runs).

## Creature: eggs at the nests of other species (2026-11-01, fix/w32-nest-eggs)
<a id="testing-nest-eggs"></a>

* `coop.nest` (main thread, BUG-0384): `objs [r]` lists the game's eggs, fruit and nests within r m of
  the avatar (`egg <ptr> pos= over_ground=<over the terrain> state= hatch= herd= family=<the avatar's
  herd> serial= mirror_of=<the host's egg serial for an egg of ours>`); `herds`, `herd <h>` (members,
  egg layer, eggs to lay, the herd's eggs); tests: `kill <h> <n>`, `respawn <h> <ms>` (+0x11C),
  `target <h> <n>` (+0xF4), `hatch <egg> <ms>`, `eat <egg>` (cEgg::SetState(4), what the eat node does),
  `look x y z [d]` (the avatar d m from a point, for screenshots). No args: the sync counters.
* `nest_eggs` (`coop_nest_eggs.py`, ~50 s warm, ~95 s cold): the nearest nest of another species misses
  2 members (`target` + 2, `respawn` 2 s); the game's own layer lays 2 eggs within ~20 s. Killing members
  does not work as a trigger: corpses stay members for minutes. An egg in the nest's bowl lies 0.74-1.2
  m over the terrain (the nest model), the check allows -0.5..1.3 m. Screenshots `eggs_host` / `eggs_guest`.
  `SPORE_FIX_OFF=nest.eggs` fails (the guest has 0 of the host's 2 eggs).
## Walk tests at 30 fps and over the player network (2026-11-01, fix/w30-npc-walk)
<a id="testing-walk-30fps"></a>

* Suites `puppet_walk_fps30`, `puppet_walk_player`, `npc_walk_fps30`, `npc_walk_player`, `tribe_walk_fps30`,
  `tribe_walk_player` (`SPORE_FPS=30`, `_player` also `SPORE_NET=player`; cold) and `puppet_walk_novel`
  (`coop.track novel 1` on both games: the partner's states without their speed;
  `SPORE_FIX_OFF=creature.puppet_path` fails).
* The walk tests' speed windows span at least 4 frames (`WIN_FRAMES`; at 60 fps still 0.1 s); puppet_walk's
  "walked", window and on-screen counts scale with the observer's frame rate (200 / 50 / 150 at 60 fps).
* puppet_walk and npc_walk wait (`settle`) until both games ran three 1 s windows of at least 20 frames
  (up to 40 s) before they measure; puppet_walk puts the walker next to the observer before each walk.
* puppet_walk / npc_walk write their per-frame rows to `out/lane<N>/track/*_<time>.json` (one file per run).
* Pitfalls: four lanes at once pushed the 30 fps games to 21-26 fps and one test killed the other's games
  when a second `lab.py run` took a lane still in use (start a run only after the last one on its lanes
  ended); the known caught startup fault at 0x11ECE4F on both games fails a run by the fault gate.

## Session paths under players' conditions (2026-11-01, feat/w33-tests-session)
<a id="testing-session-paths"></a>

Tests of whole session paths (stage changes, rejoins, joins at a busy moment, votes, mixed versions) with
the network and frame rate of real players; every check reads the other machine's game:

* `coop_latency_step.py` (suite `latency_step_ingame`, ~6 min, cold, `SPORE_NET="delay=30,jitter=5"`): one
  player walks 62 s by real input (coop_puppet_walk.walk) while the guest's proxy changes its one-way delay
  30 -> 300 ms at 10 s and back to 30 ms at 42 s (`netem.proxy().p['delay']` is read per packet, so a test
  can step it at any moment); both directions. Two checks over the observer's coop.track:
  * steps: per 1 s, BAD when the drawn puppet moves in steps (< 60 % of its >= 4-frame windows at 0.6-1.4x
    the path's speed, or > 20 % standing) or jumps (puppet_walk's rule); the longest stretch of BAD seconds
    with no GOOD one between must be <= 5 s;
  * truth: both games run on one machine and both tracks start together, so the observer's drawn puppet can
    be compared with the walker's own position `lag` s earlier (the best lag of 0-1 s per second): in the
    high phase at most 25 % of the walking seconds may be over 0.15 m median.
  `SPORE_FIX_OFF=net.offset_window` (BUG-0133) passes the step check (the creature driver keeps the drawn
  puppet smooth) and fails the truth check: the puppet is rendered past the newest state (best lag 0.20-0.30 s
  at 300 ms one way instead of 0.40-0.42 s with the fix) and is 0.15-0.27 m off the real path at turns and
  stops in 29-31 % of the walking seconds; with the fix 0-11 % (0.01-0.17 m). A second fix-off run passed
  that (10 % / 22 %), so the truth check also wants the median best lag from 3 s after the rise to be at least
  the one-way delay (what arrived is that old; a smaller lag is a guess past the newest state): fix off
  0.18-0.26 s in the four directions of two runs, fix on 0.36-0.42 s.
* `coop_transition_net.py` (suite `transition_net`, ~20 min, `SPORE_NET=player SPORE_FPS=guest:30`): Cell ->
  Creature -> Tribe through coop_campaign.Campaign.advance (the host's History button, the guest's real
  "I'm ready", editors left with "done"), two players. After each change: the guest plays the new stage in
  the host's world, after 30 s draws the host where it is (3 reads), and neither log has a `sync-check:
  MISMATCH` line in the 60 s after. `TRANSITION_PLANT=mismatch` (`coop.diag sync force shift` on the guest
  in the last watch, no LAB-MARK) fails (lab 20261101-152859). On main a05d1f6 it found Tribe `t missing` 58 s
  after the arrival in both runs (BUG-0381, NPC tribes the host forms; fixed on main 6ac351e by tribe.npc_tribes,
  so the test allows it no more; `--allow "<kind>=BUG-NNNN"` stays for the next known bug) and Creature
  `n missing` under the brain-upgrade caption of the AddEvolutionPoints shortcut in one of two runs (BUG-0413,
  a suite allow).
* `coop_party_rejoin_net.py` (suite `party_rejoin_net`, ~10 min, `SPORE_NET=players`, 4 players): rounds
  `one` (the last guest rejoins), `swap` (Bob, who plays his own creature, and the last guest leave; the
  last joins first and takes Bob's id), `lost` (a goodbye lost by a 1 s proxy outage), `both` (the extra
  guests leave and join at the same moment). After each (and in `swap` once more while Bob is away): together,
  each guest draws every other player where it is, and every machine draws every other player as the species
  that player plays. Species keys are each game's own (a creation imported from another player gets a new
  instance key: Kirgle is 290225CD in Bob's game, 7C329711 in the others', 7CF261BB after a rejoin), so a player
  who plays its own creation must be drawn as the viewer's import of it (`coop.creations`: `player <id>
  creature: <key> '<name>'` with the subject's `mine creature` name and the census key of the drawn puppet), a
  player of the world's species as that key. BUG-0076's switches (`core.id_reuse`, `core.creations_leave`,
  both) did not fail it: the newcomer with Bob's old id was drawn as its own species (labs 20261101-144314 /
  144852 / 152157); `PARTY_PLANT=lift` (Carol's game draws its partners 5 m up after the swap, `coop.track
  lift 5`) fails it (lab 20261101-153001).
* `coop_join_during.py --case combat|editor` (suites `join_during_combat`, `join_during_host_editor`, ~4-5
  min): the guest leaves after the start and joins 4 s into the host's fight with an NPC (real bites), or
  while the host is in its editor (`coop.vote editor` alone; "done" 25 s after the join). A guest that joins
  again first plays its old copy ("together" at once) and loads the host's world a few seconds later: the
  clock starts when that load is done (`in_after_reload`). Then every second on the guest: the host drawn
  within 4 m as the species it plays, campaign_lib.census_problems empty (NPCs near the guest, alive / dead,
  10 m), the fought NPC there with the host's dead flag and health within 25 %; 3 such reads in a row, the
  first within 10 s. Combat: the host's kill is dead in the guest's game within 10 s
  (`SPORE_FIX_OFF=npc.play_death` fails: alive=1, lab 20261101-145305). Each read also wants exactly one player
  creature on the guest (no stray puppet after the host's editor; `core.strays` off did not leave one).
  Editor: while the host edits, the joined guest's own creatures should move in its game (census dumps 8 s
  apart): they do not exist at all (`0 of 0`, with every fix on too): BUG-0412, allowed by name
  (`--allow-empty BUG-0412`, printed). `JOIN_PLANT=leave` (the guest drops out right after the join) fails
  it; `JOIN_PLANT=lift` did not (a standing host's puppet stays on the ground).
* `coop_vote_net_4p.py` (suite `vote_net_4p`, ~10 min, `SPORE_NET=players`, 4 players): the evolution vote
  (three real ready clicks; it must end "everybody is ready" and every game opens an editor), an action
  vote while Dave stays in his editor (the windows show 3/4, the vote waits; Dave leaves from his editor:
  it passes within 10 s, once), Dave's return (together, drawn where and as what he is), a vote of all four.
  `VOTE_PLANT=noready` (Dave's last ready not clicked) fails (lab 20261101-153741); `world.share_drop_peer`
  off did not (lab 20261101-152552).
* `coop_mixed_version.py --old guest|host [--fixture tribe]` (suites `mixed_version_guest`,
  `mixed_version_host`, `mixed_version_tribe` in suite `mixed` only: they also take the neighbour lane as
  SPORE_LANE_B, so run them alone, `lab.py run mixed --lane 9` with lane 10 idle; `exclusive` would need lanes 1-5): the
  previous release (`--old-tag v0.7.3.3`, `git archive` built once into out/old/<tag>, its DLLs and symbol
  files copied into lane B) and this build in one session of three. Checks: every game logs every other
  player's version, Carol is in the host's world, all three draw each other where they are before and after
  two of them walk, the shared DNA total (with this build's cause tail) reaches the old game, nest eggs
  (MSG_NEST) are sent while the old game stands at a nest, Tribe chunks with the time trailer, no caught
  fault, no crash report. Both lanes get this build's files again at the end; Carol's log (the other lane's)
  is kept in out/lane<N>/mixed/. v0.7.3.3 against main a05d1f6+: all three pass (lab 20261101-155537:
  the old game gets the DNA total with the cause tail, 143 nest snapshots were sent while it stood at a nest,
  Tribe chunks with the time trailer; no fault anywhere). `MIXED_OLD_TAG=v0.7.2.4` fails (lab
  20261101-155327): no version lines before 0.7.3.x ("version unknown (pre-0.7.4)"), and the 0.7.2.4 game
  had 3256 caught faults (`exception 0xC0000005 at 0x00B0EC8C`; not analysed). Pitfalls: main still says
  `coop 0.7.3.3` (COOP_VERSION is bumped at the release), so the version lines cannot tell main from the
  release; every game of the test starts cold and SporeKit starts its log anew, so the version lines are
  read from each log's start (an offset taken before the start pointed past the new log's end).
* First runs of the never-run tests (lab 20261101-134020, lanes 9/10, main a05d1f6; reruns with the test fixes
  in lab 20261101-145528: chaos_civ_long and rejoin_race_long OK, play_tribe FAIL on BUG-0410 only): OK rejoin_awful,
  chaos_cell_long, chaos_space_long, chaos_tribe_long, chaos_tribe_awful, play_space, legless_guest_dontsave.
  FAIL and why: play_civ (one rejected guest order, open BUG-0325), play_tribe (a test bug: the chiefs compare
  took the host's own `spread= placed=` counters of tribe.place_chiefs as chiefs; also the guest's 9 click
  orders < 10, and citizens p90 1.89 m > 1.5 m), chaos_civ_long (a test bug: a random key opened a design
  editor on the guest, it rejoined inside it and waited there for the world by design; the bot now leaves an
  editor with "done" after a rejoin), rejoin_race_long (two test bugs: `twoinst.wait_guest_in` looked for
  `together 1 1` but a rejoin that reaches the host before its old entry is gone gets id 2; the monitor's
  `link` check fired during world transfers to a loading guest: pending 600 / 50-90 % resends, now left out
  while a share runs, the resends themselves are BUG-0405).
* Monitor pitfall: player ids are reused (a newcomer takes the lowest free id), so `Timeline.who_at(pid, t)`
  maps a partner line to the machine that had that id at that moment; the latest id per machine
  (`who_of`) gave false `hp` violations after the id swap of party_rejoin_net.
* The players suite on lanes 9/10 (lab 20261101-160125, 73 tests, 94 min): 63 OK. FAIL: creature_burst,
  creature_fps15burst, creature_mobile, creature_players (a test bug: with SPORE_NET set, the helper
  load_game.py that coop_two_instances.py runs imported lane.py, which binds the guest's proxy port the test
  already holds: EADDRINUSE, the host never loaded its world; netem.from_env now uses the test's proxy. In
  creature_fps15burst the test then hosted on a game still starting and it crashed: BUG-0433),
  party_creature4_mobile / _players (sync-check gate: a rejoined guest's empty old copy, BUG-0412),
  sync_burst / sync_fps15burst (Civ vehicle p90 4-7 m, BUG-0438), puppet_walk_burst (the guest walked 22
  windows only, 82 % matched) and puppet_walk_players (84 % matched, limit 85 %): the walk checks are at their
  edge under loss bursts and spikes.
## Tribe sync tests and the log gates (2026-11-01, fix/w32-tribe-sync)
<a id="testing-tribe-sync-gates"></a>

* `tribe_npc_form` (coop_tribe_npc_form.py, BUG-0381, cold: the tribes it makes stay in the host's
  world): `coop.tribe npc_tribe <members> <metres>` on the host calls Simulator::SpawnNpcTribe that far
  from the village. On the tribe fixture the host's own tribe controller then forms two more tribes
  within a few seconds (archetypes 8 and 12, 5 members each, ~150 m away): the guest gets all three from
  the fix (`npc_tribes_made=3` in `coop.tribe`), none without it. The test checks the guest's game's own
  tribe list (`tribe N at (...): M members species=XXXXXXXX archetype=A`, new columns) within 15 s, before
  the sync-check's first recheck: the FAIL run has no MISMATCH line, the check fails on the tribe list.
* `creature` (coop_two_instances.py, BUG-0390): the guest's test hit goes to a proxy outside the host's
  pack and family; the host's `coop.npc list` tags them ` pack` / ` family`. On the creature fixture 4 of
  the 20 spawned proxies are the avatar's herd, and the first listed was one of them in 2 of 5 runs.
* `play_tribe` (nightly, never run before 2026-11-01): 2 runs on lane 7, no sync-check MISMATCH, both
  failed on their own checks (BUG-0397: the chiefs compare, fixed; a host bot whose box selects all
  missed in one run; citizens' p90 1.9 m in the other).
* Log gates on lane 7 with diag.rts_cursor (BUG-0380) in: chaos_tribe 2/2, tribe_coop 2/2, tribe_play 2/2,
  play_tribe 2/2 without any sync-check MISMATCH (out/lab/20261101-115510).
## The monitor's fields from the mod: drawn, ground, puppet health, posse hits, unlocks, proxies (2026-11-01, feat/w32-monitor-diag)
<a id="testing-monitor-fields"></a>

The hooks of [testing-monitor](#testing-monitor) are active: the mod writes their fields and
`monitor.py` checks them in every test and campaign (`monitor.FIELDS` has the exact output, player
logs carry them too: [REPORTING.md#reporting-monitor-fields](REPORTING.md#reporting-monitor-fields)).

* **Timing**: the `trace:` lines are written at the session clock's 10 s marks (`[s 20.0xx]` on every
  machine, within the clock's error; local 10 s without a session clock; a stage change still writes
  at once). So a partner line of one machine and the partner's own line of the same mark describe the
  same moment (measured 2-40 ms apart), which the drawn / ground / puppet_hp rules need.
* **Read at the start of the frame**: on a frame that writes the trace, `monitor_frame_start`
  (coop.cpp, right after the stale-world check, before any message, module tick or puppet driving)
  reads each partner's puppet through its adapter (`fill`: Creature `game::position`, health, max
  health; Cell / Space the object's own fields; Tribe / Civ the pointer marker). That is what the
  game drew last (the BUG-0360 measuring pitfall: after the driving it is our own target).

| rule | field (end of the line) | violation |
|---|---|---|
| drawn | partner `drawn=(x y z) drawn_v=<m/s>` | the drawn position over `DRAWN_BASE[stage]` (Cell / Creature / Tribe 3 m, Civ 6 m, Space 12 m) + `drawn_v` x (0.3 s + rtt + the two lines' time apart) from the partner's own `pos=` of the same mark, on 2 lines in a row; Tribe / Civ only while the pointer rests (`drawn_v` <= 0.5: a moving pointer's copy trails, tests pin it in jumps) |
| ground | own / partner `agl=<m>`, Tribe `agl=<lowest> agl_hi=<highest>` of the player's members | under -0.5 m on 2 lines in a row (own avatar, puppet, members); a Creature puppet over 3 m while its player's own line says under 1 m (that covers jumps, rocks, water: the player's own height explains the puppet's); Tribe members over 3 m. Not the own creature over 3 m (`GetHeightAt` is the terrain only: 1-16 m on a rock) |
| puppet_hp | partner `puppet_hp=<hp>/<max>` (read from the puppet creature) | differs from its player's own `hp=` of the same mark (max by 0.5, hp by 1 or 10 %) on 2 lines in a row |
| posse_damage | `damage: <victim> hit by <kind> owner=... posse=<0|1|?> amount=<hp> [how]` (npc.cpp, where a hit on a player is applied, forwarded or dropped) | `posse=1` with an amount over 0, at once |
| unlocks | own `parts=<count>:<hash>` (Creature) | a guest's count under the host's on its lines 70 s and 60 s before (same world), on 2 lines in a row |
| proxies | guest own `proxy_under=<n> proxy_stuck=<n>` (Creature / Tribe, proxies under the ground for 1 s / over 3 m off the host's NPC for 2 s since the last line; `npc: proxy <id> ...` names the first) | over 0 on 2 lines in a row |

Calibration (lane 6, `out/lab/20261101-120947-feat-w32-monitor-diag` smoke + puppet_walk and
`20261101-123454` tribe_coop / chaos_tribe; per field over every trace line with the field):
* drawn: Creature 40 lines, 0.30 m median, p90 0.84 m, max 1.28 m (at most 0.43 of the limit, walking
  in puppet_walk 0.68 m); Space 18 lines, p90 1.21 m, max 12.8 m at speed (0.22 of its limit); Cell 14,
  max 0.42 m; Civ 6, 0 m; Tribe 70, 0 m at rest, up to 65 m while chaos_tribe's pinned pointers jump
  (hence: at rest only).
* agl: Creature puppets -0.05 to 0.42 m (the kSink placing: -0.05), own creatures 0.00 to 0.48 m except
  one guest at -0.94 m after its species swap (BUG-0400); Tribe members -0.01 to 0.00 m (host and guest).
* puppet_hp: 40 lines, equal to the player's own hp and max hp every time.
* parts: 54:C81D84FB on host and guest (the fixture's collected parts, 312 unlockable).
* proxies: stuck 0; under 1-4 proxies on the guest's first two Tribe lines after joining (BUG-0399).
* damage: a forwarded NPC hit (`posse=0`) and the guest's `from the host` line (`posse=?`).
* Both new findings (BUG-0399, BUG-0400) are allowed in `suite.ALLOW_ALL` with their tickets.

Planted problems (each rule must fire; `monitor_plant`, tools/tests/coop_monitor_plant.py, full suite,
lab gates `monitor` and `sync-check` off for it: it judges the violations itself): `coop.parts unlock 3`
on the host only (unlocks), `coop.hp packhit 1 0.5` with `npc.party_peace` off (posse_damage; with it on
the dropped hit is no violation), `creature.partner_hp` off on the host while the guest's hp is held at half (puppet_hp),
`coop.puppet hold on` on the guest while the host walks away (drawn), `coop.npc pwalk` of one proxy on the
guest (proxies). ground: `SPORE_FIX_OFF=creature.puppet_ground lab.py run puppet_under` (the guest's states
1 m under the host's ground while it walks; in monitor_plant a standing puppet was put back on the ground
by the game and the driving: `coop.track lift -0.9` / `-3` never showed under). Offline: gates_selftest.py `field_cases` (each rule caught on planted lines, quiet on
the lag of a fast partner, Space's limit, a rock, a dropped party hit, a guest that gets the new parts in
time, one stuck line).

New test helpers: `coop.parts [unlock <n>]` (the field's count and hash; unlock n locked parts in this
game only, `cCollectableItems::UnlockPart`, docs/re/creations.md#creations-unlock-part), `coop.hp packhit
<player> <hp>` (host: the first pack member hits that player's puppet through the damage filter).
Pitfall: `tools/tests/monitor.py replay` over whole lane logs mixes sessions (the session clock starts
at 0 at every join): cut them where `[s ...]` goes back.

Evidence (lane 6): the rules failing on planted problems: `monitor_plant` PASS in
`out/lab/20261101-131215-feat-w32-monitor-diag` (drawn 15.7 m, puppet_hp 16/16 vs 8/16, proxies, unlocks
54 vs 57, posse_damage 0.5 hp forwarded; with the fix on the dropped pack hit is no violation); ground:
`SPORE_FIX_OFF=creature.puppet_ground` puppet_under `20261101-125249` (`partner 1's puppet 1.00 m under
the ground on 2 lines`); posse_damage in its own bug test: `SPORE_FIX_OFF=npc.party_peace` creature_party
`20261101-131058` (8 violations). `SPORE_FIX_OFF=creature.partner_hp` creature_partner_hp
(`20261101-130959`) fails on its own checks, not on the monitor: its hit lasts 3 s, the rule needs two
10 s marks. The clean run `20261101-131215` (smoke + puppet_walk + monitor_plant on lane 6): no monitor
violation in any passing test; creature failed on BUG-0390, puppet_walk on BUG-0394 (walk gait 0 %),
ui skipped (exclusive). Second calibration (that run): drawn Creature 69 lines p50 0.32 m (planted
lines aside under 1.3 m), Space 82 lines max 4.1 m (0.11 of the limit), Cell max 0.36 m, Civ 0 m, Tribe at
rest 0 m except one line right after a join (151 m: the pointer copy of an older state, one line);
agl Creature puppets -0.20 to 0.64 m, own -0.43 to 1.04 m; Tribe members -0.01 to 0.00 m.

## Walk tests on a loaded machine: what they judge, and the A/B (2026-11-01, fix/w34-walk-stable)
<a id="testing-walk-loaded"></a>

The walk smoothness tests (#testing-puppet-track, #testing-npc-track, #testing-tribe-walk, #testing-walk-30fps)
failed on main and every branch while 5+ agents ran 8+ lanes (lab load 26-34 on 12 cores, observers at 11-29 fps).
BUG-0407 classified every failure of the current driving (e20e9a3 and later) from the recorded track JSONs:

* **Measurement** (the drawn motion was right): puppet_walk 84 % / 81-91 % matched windows at 22-24 fps with 4-10 %
  of the observer's frames over 0.1 s: the off windows swung 0.25-2.4x around long frames with drawn errors of
  0.1-0.4 m (lab 20261101-135017, 20261101-141648, 20261101-143049 #7); npc_walk "1 jump": a 0.76 m catch-up in a
  62 ms frame behind a host NPC fleeing at 9.2 m/s (1.3x its speed) against a limit made of the run's top speed
  4 m/s (20261101-135017); tribe_walk "uneven 11 %": the host's own game, which no driving moves, measured 5-10 %
  uneven on the same frames (frame-time jitter 15-90 ms; 20261101-143049 #4).
* **Test input / harness**: the walker did not walk (a guest at 8-18 fps took no keys for 6 s: 99 frames, 11 windows;
  clicks that gave no walk: 26-30 windows; tribe_walk 294 frames, 135 on the screen); an observer at 11 fps with 41 %
  of its frames over 0.1 s (5 windows); npc_walk 10 of 10 on the posse_damage monitor gate: the test hit members of
  the host's own family (they follow the avatar, all 4 nearest NPCs were family) and they fought back; puppet_under:
  a console call of the camera thread timed out, its late reply was read as the next call's (the fault check parsed
  a track row).
* **Real** (tickets, still caught): puppet_slope "falls 3" (all at stops, BUG-0408: rare, 0-1 per run in 20 runs);
  the hit NPC's proxy held 4.8 m off for 1.1 s or knocked off and teleported back 2.4-4 m (BUG-0409); the puppet
  sliding in its standing animation for a whole walk (gait 0 %, BUG-0436, ~1 run in 20); tribe_coop's 3.7 m was not
  seen again in 30 runs (old test printed no per-member data); tribe_walk's `n apart` gate is BUG-0402, the Tribe
  `drawn` monitor gate on the RTS pointer (37.6 m after a minimap jump) the class of BUG-0393 / BUG-0416.

What the tests do now (BUG-0407):
* puppet_walk / npc_walk windows: the drawn way of frames i+1..j+1 against the path's way of frames i..j (what a
  frame draws was placed in the frame before); a window with a frame over 0.1 s in it or just before it is left out
  and counted; puppet_walk also leaves out windows in which the target itself jumped and counts a drawn way within
  0.1 m of the path's as matched (`WIN_SLACK`). The old numbers are printed beside the new (`unshifted`).
* puppet_walk: a walk that measured too little (frames or windows) is walked on up to 2 x `--secs`; more than 20 %
  of the observer's frames over 0.1 s: `NOT MEASURED` (the window check skipped, printed); stops are reported
  (BUG-0408: the drawn slide past the stop over the error at the stop, and a fall animation there).
* npc_walk: the jump limit is 3x the larger of the run's top speed and the NPC's own reported speed; the walkers and
  the two hit NPCs are taken outside the host's family / pack (`coop.npc list` tags), from up to 80 m.
* tribe_walk: a stop-and-go frame whose time differs from the frame before's by more than 1.5x is left out
  (`STEADY`, counted); a walk that measured too little on the guest is walked on up to 2 x.
* tribe_coop: each member's mirror against the host's path over the round (segments between host samples before
  and after the guest's), up to 8 rounds, the worst member printed.
* The camera threads share their console under a lock and survive a timed-out call; `sporectl.Console` drops a
  connection whose call timed out. `walk_selftest.py` (make test): the window measure passes a smooth puppet drawn
  one frame late at 22-24 fps with 10 % hitches and fails one that moves in steps.

A/B on main's driving (lanes 2-3 only were this branch's; the machine was not idle: load 14-41, mean 27, on 12
cores, 6-20 other games running; per-minute load in the branch's notes). Main's tests (A, fix on, lab
20261101-143049): puppet_walk 7/10, npc_walk 0/10 (the family hits), tribe_walk 8/10, tribe_coop 10/10, puppet_slope
10/10, puppet_under 9/10. The new tests, fix on (C, 20261101-154000; D, 20261101-173719) and with
`SPORE_FIX_OFF=creature.puppet_walk,creature.puppet_path,npc.walk,npc.path,tribe.mirror_walk` (B, 20261101-154246):

| test | fix on: passed | fix on: the measure | fixes off: failed | fixes off: the measure |
|---|---|---|---|---|
| puppet_walk | 15/15 | matched 90-100 %, standing 0-2 % per direction (A rescored: 88-100 %, the 88 % one of 26 windows now walks on) | 10/10 | worst direction per run matched 16-56 %, standing 17-58 %, 1-5 jumps |
| puppet_slope | 14/15 | matched 97-100 %, falls 0-1 | 10/10 | matched 18-71 %, standing 7-52 %, 2-5 jumps |
| puppet_under | 14/15 | matched 92-100 % | 10/10 | matched 33-66 %, standing 8-45 % |
| npc_walk | 15/15 | walk gait 93-100 %, matched 94-100 %, no jump (A rescored: matched 95-99 %) | 10/10 | walk gait 0-7 % (matched 97-100 %: the off driving teleports along the path) |
| tribe_walk | 13/15 | uneven 0-7 % (worst direction), matched 96-100 % (A rescored: 1-7 %, one run at 10 % NOT MEASURED) | 10/10 | uneven 19-57 % (worst direction), matched 80-97 % |
| tribe_coop | 10/10 | max error 0.17-0.75 m | 0/10 | max error 0.06-0.95 m: a position check, not a walk measure; no twin among the walk switches |

The failures of the fixed driving in C and D were real problems or another gate, none a walk measure's noise (0 of 75 fix-on runs failed on a walk measure):
BUG-0436 (the puppet slides in its standing animation for a whole walk: puppet_under#2, puppet_slope#6 of C) and the
Tribe `drawn` monitor gate on a jumped RTS pointer (37.6 m, tribe_walk#7 of C, tribe_walk#5 of D; BUG-0416's class).
Pitfall: `coop.npc list` tags the host's family and pack (` family` / ` pack`); hitting one of them makes the host's
own family fight its avatar, which the posse_damage monitor gate fails.
## Creature: the party's collected parts (2026-11-01, fix/w33-unlocks)
<a id="testing-parts-shared"></a>

* `coop_parts_shared.py` (suite `parts_shared`, ~70 s after the start, creature fixture, BUG-0401):
  the host finds a part the game's way (`coop.parts find <level>`: `cCreatureModeStrategy::ExecuteAction`
  with 0x5371F11 `{avatar, 0, level}`, a skeleton's reward with its goal card; level 1 gives one on the
  fixture; `unlock 2` is the fallback), the guest's `parts=<count>:<hash>` must become the host's within
  20 s; the guest finds one, the host's must become the guest's; the guest's own editor (the host's
  `coop.vote editor`) must offer exactly the party's parts (`coop.editor`: locked = unlockable - party,
  and still > 0: BUG-0368); the guest leaves and joins again: it still has every part. `coop.parts`
  prints `sync: ...` (parts.cpp's counters: sets / news / finds sent and received, applied).
  FAIL: `SPORE_FIX_OFF=parts.shared`.
* `monitor_plant`'s unlocks plant switches `parts.shared` off on the host for its `coop.parts unlock 3`
  (with the sync on the guest gets the parts within a second and the rule has nothing to catch).
* Measured (lane 6): a part reaches the other game within 1 s (the 1 Hz poll); the fixture has 54 of
  312 parts unlocked, a find at level 1 gives one.
## Creature tests of what the other player sees: fights, events, bodies, the pack, allies, editor soak (2026-11-01, feat/w33-tests-creature)
<a id="testing-creature-w33"></a>

Each test judges the other machine's game (the drawn creature, the game's own flags and health), not the mod's
counters. Every one has a `_player` twin (`SPORE_FPS=guest:30 SPORE_NET=player`) except the soak.

* `puppet_fight` (`coop_puppet_fight.py [--dir] [--seed] [--secs]`, ~4 min warm): both players go to the nearest
  nest of another species; one fights for 40 s (combat stance, clicks on NPCs on its screen, ability keys 1-5 and
  0, WASD holds with abilities, camera turns, walks back to the partner beyond 14 m), the other's camera keeps it
  on screen (`coop_puppet_walk.Watch`). Checks on the observer's `coop.track` rows: frame steps over 3x the
  fighter's own p99 speed x the frame time <= 1 per minute, driver teleports (`tp`) <= 1 per minute, over the
  frames in which the fighter moves (> 1 m/s) the drawn puppet against the fighter's own position at the same
  moment p95 <= 2.5 m and the constant delay that fits the drawn path best <= 0.3 s (fix on: 0.06-0.14 s, p95
  0.5-0.9 m; `SPORE_FIX_OFF=creature.puppet_path,creature.puppet_walk`: 0.50-0.54 s, p95 3.5-4.0 m, one 4.5 m
  step and one teleport per direction, lab 20261101-141158). The stance is read
  from the game (`call Simulator::cCreatureGameData::GetAbilityMode cdecl`: 0 social, 1 combat; the buttons are
  at 690 / 750, 998 in the 1440x1080 test window). The first version brought the fighter back with
  `coop.team come` (a teleport): the observer then saw an 8 m step; walks back by click now.
* `puppet_after_events` (`coop_puppet_after_events.py [--only ...]`, ~9 min cold): after a rejoin, the vote's
  editors (`coop.vote editor`, ready, "done" clicked until the editor is left), a death and respawn of each
  player (`coop.team die`, Enter on the caption), Alt+Enter twice on each game (the renderer's fullscreen flag
  must toggle and come back), CRG_ClaimNest on each game (Enter until it ends) and a world share
  (`coop.share`): each game has the partner's puppet object (`coop.status` `remote N ... puppet=`) and draws it
  within 2 m of the partner's own position (`coop.track`) within 20 s. `PUPPET_EVENTS_PLANT=hold` (the host's
  `coop.puppet hold on` after the first event) fails (lab 20261101-144621: 4.1 m after the deaths, Alt+Enter and
  the cinematic; the editor and the share made new puppets, which hold does not stop). An evolution reloads the
  guest with the host's world a few seconds after the editors (the share waits for the cinematic): the next step
  waits until both games are in the stage and together for 6 s (`settled`), `coop.team die` is retried while there
  is no avatar, and a respawn counts only after a death was seen (the `_player` run's first death found no avatar
  and its "respawn" passed on the living creature).
* `corpse_positions` (`coop_npc_corpse.py [--only] [--skip]`, ~5 min warm, BUG-0383): kinds stand, guest, run
  (`coop.npctrack walk` at 6 m/s, killed running), thrown (`coop.npc hosthit <id> <dmg> <push>`: TakeDamage with a
  push away from the avatar and up, new test argument), slope (an 065CB8BD walked onto the fixture slope; the
  walk is asked again every 0.5 s, its AI takes it back to the nest otherwise), npc (`coop.npc epic 0 6`: the
  epic killed an NPC within 1-2 s in every run), later (the guest 150 m away on land during the kill), eaten (the
  guest clicks the body's screen position from `coop.npctrack`; what eating does is printed). 3 s and 30 s after
  each death: the guest's proxy dead by the game's own flag (`coop.npc list` on a guest now ends with
  `game_dead=<0|1> impressed=<0|1>`, the proxy's mbDead and impressed flags; the old `DEAD` there is the mod's
  `killed`), its model within 1.5 m of the host's, both on their ground (model height over the terrain
  -0.6..1.2 m). All bodies were 0.00 m apart: a guest puts a killed proxy where the host's body is every frame.
  `SPORE_FIX_OFF=npc.dead_proxy` fails `npc` and `later` (lab 20261101-135546), `npc.play_death` every kind
  (20261101-145148); PASS 20261101-141938, `_player` 20261101-142305.
* `team_real_damage` (`coop_team_real_damage.py [--victim]`, ~3 min warm): the victim walks up to a host NPC of
  another species and hits it with 0.3 (`coop.npc hit` / `hosthit`) until one bites back (the 065CB8AB and
  065CB811 Kooks do within 1-3 s, their bites take 0.2-0.5); hurt with the partner 25 m away (no regen), regen 5 s
  next to it (+10 %), rescue: the victim's health set to 0.4 of one bite, the NPC's next bite would kill. Each
  judged on the other game's puppet (`coop.hp`). `SPORE_FIX_OFF=creature.partner_hp` fails the other game's
  hurt / regen / rescue checks (lab 20261101-143808). An NPC stops biting now and then: the rescue phase holds the
  victim at 0.4 of a bite (undoing the regen each second) and the victim hits the NPC with 0.1 every 3 s until a
  bite lands (up to 25 s); the first version waited 12 s and timed out in 2 of 4 runs.
* `creature_social_ally` (`coop_creature_social_ally.py`, ~4 min warm, BUG-0021): coop_creature_social's ally
  phase with the stance read from the game (clicked only when not social), the game's impressed flag per Kook on
  both games (host `coop.npc list` lines end with `impressed=<0|1>`), the ally record on both, the guest's card,
  then the host's `coop.npc recruit` of an allied Kook and a 20 m walk: the guest's proxy of it within 10 m of the
  host and 2.5 m of the host's own Kook. `SOCIAL_ALLY_PLANT=npcsync` fails the recruitment (lab 20261101-144024:
  27 m). Before `social.no_bite` the ally part failed in 3 of 5 runs on BUG-0021: the guest's own avatar killed an
  impressed Kook (host log: event 2 then event 0, `dna: ... cause=hunt:1`; the test prints `key 1 sent a HIT` /
  `the click sent a HIT` when the guest's `hits_sent` grows, with the stance social before and after). The fix drops
  a guest's hits on proxies in the social stance (`coop.npc hit` passes); its guest log says `social: a hit of the
  avatar on npc N in the social stance is not sent` (1-3 per run). Its first run reproduced BUG-0021: impressions 3 by the host's counter
  (3 Kooks with the game's flag), the species record (relation, impressed) went (0, 1) -> (4, 2) -> (3, 1), no Kook
  killed, one game refused in between. The host now logs every change of the species table
  (`social: relationship event <n> species <key> by <creature> (npc <id>): relation a -> b, impressed ...`).
* `creature_editor_soak` (`coop_editor_soak.py [--cycles]`, extended, ~30 min): 32 rounds (`SOAK_CYCLES`) of the
  vote's editors with a world share every 4, `coop.diag mem` on both after each round (free MB, largest block):
  loss <= 8 MB per round after round 3, largest block >= 128 MB. Calibration (lane 5, 12 rounds): free address
  space 3410-3470 MB, no trend, largest block 2037 MB throughout, ~45 s per round. `SOAK_PLANT_MB=40` (the host's
  `coop.diag reserve 40` after each round: VirtualAlloc MEM_RESERVE, never freed; new test command) fails (lab
  20261101-141320, `SOAK_CYCLES=8`: host 39.8 MB per round). PASS 20261101-152216 (32 rounds, 28 min). An earlier
  32-round run (20261101-143943) met every check of its own and failed only on the known cold-start caught fault at
  0x11ECE4F on both games. Both 32-round runs: the guest lost 1.8-2.2 MB of free address space per round (3504 ->
  3377, 3513 -> 3359 MB), the host 0.7-0.8; the largest block stayed 2037 MB (a large-address-aware game under
  Wine: 4 GB). Not a failure at this rate (~200 MB per 100 rounds); worth watching in longer soaks.
* Pitfall (BUG-0404): a vote started while the host's evolution cinematic (F4F96E43) still plays after an editor
  opened the next editor without its buttons and the host stayed in it with the cinematic "playing"; the editor
  helper now waits for the cinematics to end (Enter) like a player.
## Proxies on the ground: npc_walk_slope, the proxies rule after joins (2026-11-01, fix/w33-proxy-ground)
<a id="testing-proxy-ground"></a>

* `npc_walk_slope` (BUG-0406): `coop_npc_walk.py --goto=-195,-465,-45 --falls 3`: both avatars on the creature
  fixture's slope, the walkers are the host's NPCs within 60 m; the test also prints the fall starts (+0x137
  1 -> 0 while the host's NPC moves) per 100 s of walked proxy frames and of the host's own NPCs, and with
  `--falls N` fails when the proxies' rate is over the host's + N. Without npc.walk_ground: 51.1 and 22.7 vs
  0.8-0.9.
* The monitor's `proxies` rule caught BUG-0399 only after cold joins, in about 1 of 5 cold Tribe runs: a
  warm run (the guest already in the world) says nothing about it. FAIL / PASS evidence is a series of
  `--cold` runs (`lab.py run tribe_coop,chaos_tribe --cold --repeat 4`).
* `coop.npc sink <id> <m> [mark]` (guest): the proxy m under this game's ground once (without / with the
  teleport mark). In a warm game the game itself grounds it again within 0.6 s, so it does not reproduce
  BUG-0399 (docs/re/tribe.md#tribe-proxy-ground).
* `coop.npc list` lines end with the heights over the ground (`agl=`; guest: `want_agl= made_agl= grounded=`).

## puppet_fight judges what is on the observer's screen (2026-11-01, fix/w34-ability-pop)
<a id="testing-fight-in-view"></a>

BUG-0414. The game puts a creature outside the view on the plain terrain (`GetHeightAt`): standing on a nest
mound the partner's puppet is drawn up to 1 m lower while the observer looks away, and `Watch` (the camera drags of
the walk and fight tests) swings the view over it (docs/re/creature.md#creature-out-of-view-ground).
* `coop_puppet_fight.analyse(..., wh)` counts a step as a drawn jump only when the puppet is on the observer's
  screen in both frames (`coop.track` vis and sx / sy inside the window); `off_view` lists the others, `entry` a
  radial step from the first frame back in view (drawn low once, BUG-0434, not judged). It prints the share of
  frames on screen (84-100 % in fights) and, per step, the partner's animations played on the puppet in the
  0.6 s before it and the fighter's own ones (new `coop.track dump` columns `ranims ranim`: on a puppet's rows
  the MSG_ANIM played on it so far and the last id, on the own avatar's rows its own animations sent).
* Re-judged, the two failing w33 runs (20261101-142305: 3 jumps, 20261101-151044: 2) have 0 jumps on screen
  (3 out of view, 2 `entry`).
* The fighter's health is set to its maximum (`coop.hp` own max), not to 1000: the invariant monitor's
  `puppet_hp` rule failed every run on the puppet that cannot show 1000 of 10 (lab 20261101-160710, 2 of 2).
* Pitfall: a nest mound is not in `GetHeightAt`; heights over the ground (`hd`, `ht`, `agl`) of a creature on it
  read 0.4-1.03 m although it stands on its ground.
* `coop_tod_early.py` (suite `tod_early`, ~1 min, BUG-0433): the host game alone, `cheat coop host` on its
  console from the first moment (coop's commands exist from the first frame: `coop.fix` is asked until it
  answers, then the host line runs on frame 2, game mode 0). No fault, the galaxy menu, still hosting.
  `SPORE_FIX_OFF=core.tod_guard` faults at 0x01020F35 and hangs before the menu (lab 20261101-175729).
  `--ui` takes the player's way (F9 from the boot on, a real click on Host): the window first shows in the
  galaxy menu (9.2 s after the start), so players do not reach the crash through the window.

## The busy gate: votes, editors and shares during a cinematic or a pause (2026-11-01, fix/w34-vote-cinematic)
<a id="testing-busy-gate"></a>

* `coop_vote_cinematic.py` (suites `vote_during_cinematic`, ~5 min, and `busy_gate` with `--part gate`):
  starts each gated action while the host's (or the guest's) game is busy and checks the game state on the
  machine that was busy: the vote's editor (the game's mode, `cinematic`, `input_lock` in the editor, a real
  click on "done"), the guest's own editor after a vote, an action vote's result, a share due as after an
  evolution (the guest's mode leaves Creature / "receiving world"; held for a cinematic, not for a pause).
* Busy by hand: a cinematic with `PlayCinematic ... s:CRG_Fade_From_Black_Egg` (~7 s, ends by itself); the
  game time paused with a real click on the Sporepedia's HUD button (84 1054) or Esc (the pause menu);
  `coop.status` prints `busy: <reasons>` (`-` when free).
* Test hooks: `coop.share due` (a share due as after an evolution, the gate path of follow_phase);
  `coop.vote` prints `editor held for a cinematic N, cinematics ended N, editor held now N, actions waited N,
  action due N`.
* Pitfall: a vote ended by a guest right after the host's "done" lands inside the evolution's own
  cinematic (~7 s): that is the players' way to BUG-0404, and coop_editor_soak.py's helper now waits for the
  cinematics between rounds (it would test BUG-0404, not the address space).
* The campaign's sync and census checks skip a guest whose game stands (`busy:` paused, editor, loading)
  instead of only one with the "Resume Game" button shown (`OLD_PAUSE_CHECK=1`: the old check).
* Pitfall (BUG-0447): an action vote's result runs on the host at the gate, not at the vote's end: a test
  that reads `actions run` right after `coop.vote` stops saying `vote active` can read the count before the
  run (vote_net_4p 20261101-210646: 0.94 s under the "game saved" box of a world share that was just
  written). Read it with `coop_party.ran_once()` (waits for the count, then 2 s for a second run).

## Monitor gates that misjudged: rejoin transfers, a jumped pointer, the party hit (2026-11-01, fix/w35-monitor-gates)
<a id="testing-monitor-w35"></a>

Three monitor findings of the w33 runs were the rule or the test, not the mod (each with its ticket):

* **link across rejoins (BUG-0415)**: a world transfer to a guest on loopback sends up to 4x the world's bytes
  again (the LAN window of 600 reliable chunks outruns the 128-packet ack window, a loading guest acks late;
  BUG-0405 on feat/w33-tests-session): chaos_tribe 20261101-150122 #3 had two rejoins 16 s apart, the host's
  partner line said resends/sent 0.66, pending 401, 0.99 on the three lines from s 50 (`out 17181 KB` for a
  3442 KB world). The rule now leaves out a partner line while the host sends its world to that partner and
  15 s after "queued completely" (`Timeline.sharing`, the same code as feat/w33-tests-session's; config
  `share_after`, < 0 = the rule before). A storm outside a transfer (or one that goes on after it, or a transfer
  to another player) still fires: gates_selftest `resend_after`, `resend_other`.
* **drawn on a Tribe / Civ pointer (BUG-0416)**: `drawn_v` is the copy's speed over the newest states this game
  has (`r.buf.sample(now)` and `now - 0.5`), not the drawn copy's. A pointer that jumped a few frames before the
  mark has a copy still resting at the old point with `drawn_v` 0 only while no state with the new point has
  arrived: a late state, as after the host's 0.8 s save hitch at s 99.9 of chaos_tribe 20261101-140615 (164 m;
  its sync-check at s 99.1 had compared both pointers OK). Own trace lines in Tribe / Civ now end with
  `ptr_still=<s>` (seconds since the own pointer last moved, modes::rts_marker_still); the drawn rule judges a
  copy only when its player's pointer stood still for `drawn_still` (1 s) + rtt (< 0: the rule before). A
  pointer that has rested for a second is where the copy must be: the rule still fires on it.
* **posse_damage in npc_walk (BUG-0417)**: the walkers (the proxies nearest the guest) were often members of
  the host avatar's family; the flee phase's `coop.npc hosthit` (the host's avatar as attacker) made the family
  fight the host back: the game's own answer to the host attacking its own party, a hit only the test makes (a
  guest's hit on the host's family is dropped by npc.party_peace; no hit path of the mod is involved). npc_walk now walks and hits NPCs outside the host's pack and family (BUG-0390's rule);
  `NPC_WALK_PARTY=1` puts them first again (the gate fails).
* `monitor_tribe` (coop_monitor_tribe.py, ~3 min, tribe fixture, monitor and sync-check gates off: it judges
  the monitor itself): (1) the host runs 600 ms frames (`frame.limit 600`) and pins its pointer 176 m away in its
  first frame after three marks in a row (the command sent 0.03 s before the mark; the session clock from
  `coop.diag sync`): the guest's line at the mark has the copy 175.9 m off with drawn_v 0.0, the host's
  ptr_still=0.0, and no drawn violation; (2) `coop.puppet hold on` on the guest and the host's pointer pinned to
  a third point: drawn reported within ~20 s; (3) two rejoins in a row: no link violation. FAIL with
  `SPORE_MONITOR='{"drawn_still": -1}'`. Pitfalls: a point the copy already shows plants nothing (the first
  version pinned the jump phase's last point again); with 60 fps games a pin 0.06 s before the mark landed only
  when the rtt happened to be high (1 of 2), because the new state arrives within a frame.
* Seen on the way: a guest whose mouse never was over its planet (the lab's guest windows) sends its Tribe
  pointer as (0 0 0): the host draws it at the planet's centre (`drawn=(0.0 0.0 0.0)`, d=506). Harmless (not
  visible); the drawn rule compares it with the guest's own (0 0 0) and agrees.

## Report screenshots test (2026-11-01, feat/w35-report-shot)
<a id="testing-report-shots"></a>

* `coop_report_shot.py` (suite `report_shot`, warm Creature, ~1 min, `reports=True`: it writes
  archives): the guest's `/bug` gives `shots/mark-...jpg` in its archive (a JPEG <= 1280 wide with the
  back buffer's aspect, 5-500 KB, mean brightness within 30 of a `shot` of the same frame), listed
  next to its MARK in the report; the capture costs the frame under 100 ms, no `perf: hitch` after;
  the host's "Report a problem" click puts `shots/report-...jpg` into its archive and the window says
  "A screenshot of the game is included". `--off` / `SPORE_REPORT_SHOT=0` writes `[report]
  screenshot=0` into the lane's coop.ini for the run (restored after): the test must FAIL then.
* Pitfall: a warm guest's screen is black for some seconds after its join (only the overlay is
  drawn): the test waits until a `shot` of the guest shows the world before it marks, else the mark's
  screenshot is (rightly) black.
## Eggs hanging in the air (2026-11-01, fix/w35-egg-kick)
<a id="testing-nest-eggs-air"></a>

* `coop.nest objs` prints `over_surface=` for an egg: its height over the surface on its radial line as
  cEgg_SnapToGround finds it (the terrain, or the nest mound / a rock where their collision is loaded,
  near the camera); `over_ground` is over the bare terrain only (0.74-1.2 m in the nest bowl). A snapped
  egg lies at 0.00. `coop.nest lift <egg> <m>` puts an egg m higher (SetPosition): it stays there unless
  a creature wakes it (eggs have no gravity while asleep, docs/re/creature.md#creature-egg-gravity).
  `coop.nest` (no args) shows `ground_fix=` and `grounded=` (eggs the host put onto the surface).
* `nest_eggs_air` (`coop_nest_eggs_air.py`, ~80 s warm, ~110 s cold, BUG-0443): the herd lays 2 eggs, then
  one is lifted 2 m; the host 6 m and the guest 8 m from the nest so nobody wakes the eggs (the guest's
  creature put onto an egg woke the other one, which fell). `SPORE_FIX_OFF=nest.egg_ground` fails: one in
  two laid eggs hangs 0.3-0.5 m over the surface and the lifted one stays 2 m up (screenshot `air_host`).
* Far from the host's camera the nest mound has no collision: the surface there is the bare terrain, so
  a check of eggs over the surface needs the host near the nest (the fix only acts within 60 m).
## The guest's paused game and its mirrors' ground: tribe_mirror_ground (2026-11-01, fix/w36-tribe-ground)
<a id="testing-tribe-mirror-ground"></a>

* `coop_tribe_mirror_ground.py` (suite `tribe_mirror_ground`, warm, ~1 min; ~1.3 min cold), BUG-0444: the
  guest's game time held paused (the state a cold join leaves for 10-40 s: `cGameTimeManager` +0x48 = 3,
  written with `mem.write` at `[0x167EB04]`+0x48, GOG 2024 only; resumed at the end), the guest's own member
  walked to the steepest rise within 60 m of it on the guest's ground (16 directions, `coop.ground`) and back,
  sampled every ~0.3 s (`coop.tribe` + `coop.ground`); then `coop.tribe lift -2` on the guest and
  `coop.tribe spawn 2` on the host (the new mirrors made at host positions 2 m under the guest's ground; lift
  now applies at the spawn too). PASS: the walked mirror between -0.5 and 1 m over the ground, under 3 m off the
  host's member at the end, the new mirrors not under -0.5 m. `SPORE_FIX_OFF=tribe.mirror_ground` fails
  (5-9 m under, up to 10 m over, the new mirrors at -2.0 m; the monitor's `ground` rule too).
* Pitfall: a cold join's own pause ends by itself during the test, so the test writes the flag on every sample;
  a manual run right after `lab.py reload` started its games cold here (seen 3 times on lane 10, cause not
  looked at), and then prints `after the cold join`.
## Creature: the party's parts after Cell -> Creature (2026-11-01, fix/w36-parts-transition)
<a id="testing-parts-transition"></a>

* `coop_parts_shared.py --from-cell` (suite `parts_transition`, ~5 min, `cell` fixture, BUG-0441): the host
  changes Cell -> Creature the players' way (coop_campaign.py `Campaign.advance('cell')`: food bar, History,
  the guest's real ready, the editors left with "done", about 90 s), then the guest's `parts=<count>:<hash>`
  must be the host's within 10 s; then the steps of `parts_shared` (host find, guest find, the guest's own
  editor, rejoin), where the guest's editor must also offer the host's very parts (`coop.editor`
  `available=<count>:<hash>`, the palette's IsUnlocked over every item, against the host's `parts=`).
  FAIL: `SPORE_FIX_OFF=parts.census_states` (the guest counts 49 against the host's 55).
* `coop.parts keys` lists the unlocked keys, `coop.parts states` every item state (`key=state`, `u` =
  IsUnlocked): diff the two games' lists to see which parts differ.
* Measured (lane 9): after a live Cell -> Creature start the host's census has 324 items (the `creature`
  fixture 312); a loaded game counts 324 too with the switch on.
* Pitfall: after the players' Cell -> Creature the host's game shows the stage's intro caption ("It is the
  beginning of a new day...", cinematic 0A34E41F, `coop.status` `cinematic 0A34E41F playing`, trace
  `input_lock=1`) and waits for its green check (1410, 1048) for as long as nobody clicks it (minutes in
  transition_net, until the History click). A `coop.vote editor` during it left the host's editor black and
  stuck (BUG-0404, fixed on fix/w34-vote-cinematic); the test clicks the check like a player first.
* `coop_physics_orphan.py` (suite `physics_orphan`, warm, ~1.5 min cold), BUG-0449: `coop.physorphan` on the
  guest in Tribe makes the next physics update find a live body of kind 0 without an owner (the slot past the
  body pool's high water made live, as the body the guest's game faulted on in tribe_walk); the guest's `exc`
  must stay at 0 and `coop.status` must count one body left out. `SPORE_FIX_OFF=game.physics_owner` faults at
  0xB4C5B4 in every frame and FAILS. The physics update runs only while the guest's game time runs (the test
  waits up to 60 s for the stand-in to be made).

## Divergence detector (2026-11-02, feat/w39-detector)
<a id="testing-detector"></a>

Step L0 of [sync-architecture-plan.md](sync-architecture-plan.md): the game's own world on the host and
on a guest, compared, so unsynced systems are found by the lab, not by players.

* `coop.census all [r]` (mods/coop/census.h): besides the v1 rows, every placed game object (every
  cGameData with a cSpatialObject in cGameNounManager's list) within r m of the own player (default 250,
  at most 6000), the Cell stage's foreground cells (host: its pool with the cell index; guest: mirrors
  with the host's index, its own cells as `local`), `scale=` / `noun=` on every row, the own position
  (`me pos=`) and the count of every noun type in the whole list with the Ghidra address of one object's
  vtable (`nouns <noun>=<count>@<vt>`: the exe has no RTTI, the class is found from the vtable).
* `tools/tests/worlddiff.py`: `diff(host, guest)` around the guest's own player (Cell 40 m, Creature
  70 m, Tribe 90 m, Civ 160 m, Space 120 m). Matching: by net id (creature npc / proxy, Cell index), else
  by noun type and the nearest position (3 m static, 15 m moving). Findings, one per signature and
  comparison, `<stage>:<what>:<kind>`: `host_only` / `guest_only` / `unmatched` (objects of a kind near
  the guest on one game only), `guest_local` / `host_local` (objects the mod does not track), `alive`,
  `scale`, `model`, `hp`, `pos` (matched objects that differ), `noun_only_host` / `noun_only_guest` (a noun
  type in one game's whole list only). `SPORE_LANE=N python3 tools/tests/worlddiff.py` diffs a running lane.
* Live in any test: `SPORE_DETECTOR=<s>` makes the monitor (monitor.Watch.detect) compare the host with
  every guest every s seconds and print `DETECTOR-SAMPLE {...}` / `DETECTOR {...}` lines into the test log.
  Not a gate: tests pass or fail as before.
* `tools/tests/coop_detect.py --fixture <f>`: a quiet session of one stage, a comparison every 20 s (fails
  only on a broken dump or a fault). Suite `detector`: `detect_<stage>` for every stage, the play bots
  (`play_*`) and the campaigns.
* The night run: `tools/nightly_detect.sh` (lanes 2-7, 9, 10 that are free, 3 rounds, `SPORE_DETECTOR=30`),
  then `tools/detector_report.py --since <YYYYMMDD-HHMM>` writes `out/detector/<date>.md`: per stage,
  each signature with how many comparisons had it, the tests, an example and the ticket that covers it
  (`KNOWN` in the script; `**new**` otherwise).
* First runs (2026-11-02, lanes 9-10): Cell quiet session: no finding in 8 of 9 comparisons. Creature quiet
  session: `scale:creature` in every comparison (NPC proxies 0.93 where the host has 0.80: BUG-0461,
  found without a player), `unmatched:fruit` (about half of the fruit lie elsewhere on each game: fruit is
  local), `guest_only:018C84A9` (a noun type only the guest's game has, near its avatar: class not known yet).
* Tribe / Civ / Space (2026-11-02, lanes 9-10, after the RTS centre fix: a background game's pointer is
  (0 0 0), so the dump takes every object and the compare centres on the host): Tribe `host_only:egg` /
  `noun_only_host:egg` (eggs in the host's game only), `unmatched:fruit`; Civ `unmatched:citizen` (about
  half the citizens elsewhere) and `scale:citizen`; Space `scale:018EBADC` (4.95 host, 3.30 guest) and
  two noun types never at the same place. Unknown noun types carry their vtable for Ghidra.
* Triage of the first full night (2026-11-03, out/detector/20261103-final.md): every signature has a ticket
  (BUG-0461, BUG-0472..0480, BUG-0483..0490; `KNOWN` in tools/detector_report.py) or is not compared by
  design (`worlddiff.IGNORE`: invisible hit spheres, the terrain cursor, missions, orders, inventories, and
  the Space level of detail: `visible_planet` and a planet's creatures / citizens in the whole lists, which
  only the game whose camera is on that planet loads). A new `**new**` row in a later report is a new
  finding: give it a ticket or an IGNORE entry with the reason, never leave it.

## Mirror sizes and the NPC resync (2026-11-03, fix/w40-scale)
<a id="testing-mirror-scale"></a>

* `coop_mirror_scale.py --kind npc|tribe_member|ufo` (suites `mirror_scale_npc`, `mirror_scale_tribe`,
  `mirror_scale_space`, warm, about 80 s each): the size the game gives each original and its mirror on the
  other game (GetScale: the census `scale=` for NPC proxies and the players' ships, `coop.fields list
  tribe_member` for tribe members), 4 rounds 5 s apart after 20 s; FAIL when more than 5 % of the pairs differ
  by more than 12 % (the detector's tolerance). It prints the detector's `<stage>:scale:<kind>` per round.
  Twins: `SPORE_FIX_OFF=npc.scale`, `tribe.scale`, `space.puppet_scale`.
* `coop_npc_resync.py` (suite `npc_resync`): the guest destroys three of its NPC proxies without telling the
  host (`coop.npc forget <id>`); 10 s later each must be a creature of the guest's game again, alive as on the
  host and within 6 m. With the fix the spawn is back within about a second. Twin: `SPORE_FIX_OFF=npc.resync`.
* Pitfall met on the way: a shell loop `until ! pgrep -f <script>` waits for itself (its own command line
  holds the pattern), the same trap as `pkill -f` (CLAUDE.md 3): wait on the background task, not on pgrep.
## World objects owned by the host (2026-11-03, fix/w40-world-objects)
<a id="testing-world-objects"></a>

`tools/tests/coop_world_objects.py --kind <k>` (mods/coop/worldobj.cpp, nest.cpp;
docs/re/creature.md#creature-world-objects), warm, ~1-3 min each. Every kind asserts the game's own state in
the other player's game, both ways where the kind has two:

* `wobj_ornament` (BUG-0459 / BUG-0473): a bone with a part reward at the same place on both games; the guest
  uses it (`coop.wobj use`: what the game's interact node does at its end), the host's reward is used up, the
  host's avatar uses it too: one part more on each game (`coop.parts`), not two.
* `wobj_fruit` (BUG-0472): two fruit on the ground at the same places; the host eats one (`coop.wobj eat`:
  cFruit_SetState 5, the eat node's end), it is gone from the guest's game; the guest eats the other.
* `wobj_tree` (BUG-0472): a fruit falls from a tree (`coop.wobj drop <tree> <slot>`: the flora's DropFruit)
  in one game: the same tree's mask in the other game loses the slot and a fruit lies under it, both ways.
* `wobj_herd` (BUG-0460): the host's avatar kills the creatures of the nearest nest of another species until
  the species is extinct: the herd +0x190 / nest +0x120 in the guest's game too.
* `nest_eggs_tribe` (BUG-0474, tribe fixture): every egg at the animal nests in the host's game is in the
  guest's within 1.5 m (`coop.census all`).
* `coop.wobj` prints per kind the binds, applied / dropped / missing counts and the requests;
  `coop.wobj list <kind> [r] [x y z]` this game's objects of a kind with the host object each is bound to.
* Each fails with its switch off (`SPORE_FIX_OFF=wobj.ornament|wobj.fruit|wobj.tree|wobj.herd|nest.eggs_tribe`).
* The detector's `coop.census all` leaves fruit hanging in a tree (cFruit +0x16C state 0) out of its rows (the
  noun counts keep them): each game's flora makes them near its own camera, so a walking guest had 15 "unmatched"
  hanging fruit at trees whose masks were equal on both games. The trees are compared by `wobj.tree`. Rocks
  show the same pattern (BUG-0498, 29 at once on the walking guest only, gone again): not left out yet.
* Detector numbers of this branch (24 comparisons per stage each, SPORE_DETECTOR=20): switches off
  out/lab/20261103-130419: `Creature:unmatched:fruit` 96%, `Tribe:unmatched:fruit` 92%, `Tribe:host_only:egg`
  54%; on out/lab/20261103-131403: 46%, 8%, 0%.
## Editors, DNA and Cell parts between the players: editor_sync, dna_spend, cell_parts (2026-11-03, fix/w40-editor)
<a id="testing-editor-sync"></a>

* `coop_editor_sync.py --case guest` (suite `editor_sync_guest`, ~3 min, creature fixture): +40 DNA on
  the host, `coop.vote evolve`, the guest's "I'm ready", both editors open (real input); the host leaves
  with done, the guest drags an unlocked mouth (its own editor locks the species' uncollected parts:
  the first column's lower rows at y 780) and presses done. Checked: the guest's game's DNA drops by the
  price and stays there, the host's total too (BUG-0466; `SPORE_FIX_OFF=creations.own_editor_dna` fails).
* `--case spend` (suite `dna_spend`, ~1.5 min): `coop.dna add -25` on the guest (AddEvolutionPoints in its
  own game): the host's total and the guest's game both lose 25 (`SPORE_FIX_OFF=core.dna_spend` fails).
* `--case host` (in no suite): the host edits and evolves, the guest's puppet should take the host's new
  species (BUG-0457). The host's evolution is not applied in the test yet (docs/re/creations.md#creations-own-editor-dna).
* Pitfalls: `coop.vote editor` opens the "edit a creation" editor also for the host: its done never
  evolves the species and costs no DNA. +150 DNA on the creature fixture grows the brain: a paused
  caption holds the vote's editor for 60 s (Enter on the caption: `cinematic ... playing` in coop.status).
  The DNA shown in an editor is the editor's own budget: `coop.dna` reads the game's value, which changes
  only when the editor closes.
* `coop_cell_parts.py` (suite `cell_parts`, ~1 min, cell fixture): `coop.parts cell collect <i>` on the
  guest (the game's own CollectPart), the host's part state and mating-call button within 15 s, then the
  other way (BUG-0467; `SPORE_FIX_OFF=parts.shared_cell` fails). `coop.parts cell` prints the 13 states.
## NPCs and mirrors of objects that go away (2026-11-03, fix/w40-crashes)
<a id="testing-lost-objects"></a>

* `coop_npc_lost_target.py` (suite `npc_lost_target`, ~2 min, BUG-0453 / BUG-0456): the guest hits a host
  NPC through the damage filter (`coop.npc hit <id> 0.3`) until the NPC is in its attack node 0x02D852E6
  with the puppet as its target (host: `[npc+0xE7C] == puppet+0x5A8`, `[[npc+0xB4C]+0x1D8]`), then
  `coop.puppet respawn 3` (host: every partner's puppet is destroyed now and shown again 3 s later, as
  on a death) and reads every NPC's 8 stimulus slots for the destroyed puppet; then `coop.attackcut`
  (the next NPC in its attack animation gets SetTarget(null)) twice. SPORE_FIX_OFF=npc.attack_lost_target,
  npc.stale_sender: ~135 faults at 0xD65D23 and ~30 stale senders. A removal alone hits the crash only
  when the attack animation plays in that frame (none in 3 runs of 6-8 removals): the armed cut is the
  deterministic half. The puppet's own 3 s hide makes a `puppet` monitor line (allowed in suite.py).
* `coop_cell_unknown_model.py` (suite `cell_unknown_model`, ~1 min, BUG-0455): `coop.cell spawn <slot> dx
  dy <level> <model instance hex>` on the host makes a copy of an NPC cell and sends it to the guests
  with that model instance (cellsync::set_fake_model; 0 clears it). SPORE_FIX_OFF=cell.unknown_model:
  the guest faults at 0xE83EC8 (read 0x9C) and makes no further mirrors.

## Creature: sneak, relations, NPC animations (2026-11-03, fix/w40-creature-acts)
<a id="testing-creature-acts"></a>

* `creature_sneak` (coop_creature_sneak.py, ~1 min warm): each player sneaks (`coop.sneak on`, the game's setter;
  the guest's fixture creature has no sneak ability, so `coop.sneak force`); the other game's puppet is read with
  `coop.sneak` (+0xBB1, noise radius +0xB80, alpha +0xB7C, model alpha). The alpha eases over 2-5 s, the test
  compares it with the actor's own after 3 s. `SPORE_FIX_OFF=creature.sneak` fails.
* `creature_relations` (coop_creature_relations.py, ~1 min warm): `coop.social rel` lists per NPC id the game's
  GetRelationship(avatar, npc), CreatureSocialMode(npc), the tuning and its key; the guest's proxies are made again
  after the switches (`coop.npc forget`, the host resends) and put into the state of a proxy made in a herd of ours
  (`coop.npc untune`). `SPORE_FIX_OFF=creature.relations` and `SPORE_FIX_OFF=npc.tuning` fail.
* `coop_npc_anims.py` (not in a suite: BUG-0464 open): samples `coop.npc cur` (vt 0x58 GetCurrentAnimation per NPC
  id) on both games for 40 s; fails on this code (7-9 % of the samples match).

## The play_* bots: what their chronic failures were (2026-11-03, fix/w40-play)
<a id="testing-play-chronic"></a>

* play_tribe `sync t*: 90% within 1.5 m` (0 of 9 runs, BUG-0481) was the measurement: it compared a guest's
  member with where the host has it now, but the mirrors follow the host's states 0.2 s back on purpose
  (docs/re/tribe.md#tribe-mirror-lag); the members run at 8-9 m/s, so 0.22 s x 8.4 m/s = 1.85 m p90 while the
  mirrors were 0.09 m p90 off the host's path. play_tribe now reads the host's coop.sync back to back for
  0.45 s (0.6 s in the measuring runs) before each guest read (the probe: `probe standing / walking` lines, best lag, rows in
  `out/lane<N>/track/play_tribe_probe_*.json`) and compares a citizen with the host's path over 0.32 s
  (TRIBE_LAG: the 0.2 s delay + one 0.12 s send interval), as coop_sync.py does with its LAG_WINDOW; the raw
  p90 is printed. Planted desync: `PLAY_TRIBE_GUEST_CMDS="coop.tribe delay 0.6"` (mirrors 0.6 s late) fails
  it (p90 2.71 m, best lag 0.60 s, out/lab/20261103-152720). The guest's minimap calibration prints the three
  points and the busy state when it fails (seen once, 20261103-012624 #3, not analysed).
* play_creature: the brain upgrade caption ("Brain power!", cinematic F6A4B1E1, after 100 DNA) waits for OK;
  the bots did not answer it and stood in it for the rest of the run: no hunts, `joint hunts happened (+0)`
  (BUG-0500) and the chat lines typed under its input lock were lost (a real bug, BUG-0482, fixed by
  `core.ui_locked_edit`). The bots press Enter on it now (`caption_ok`).
* play_creature `sync p*: never more than 6 m apart` on one sample right after a death (BUG-0499): the
  puppet slid after its creature's respawn / egg jump; `creature.puppet_jump`. Test `coop_puppet_jump.py`
  (`puppet_jump`, ~1.6 min cold): each player dies, the other's coop.track: the puppet within 1 m of where a
  jumping target landed within 0.15 s (off: 0.29 s for the 6 m egg jump).
* `coop_chat_caption.py` (`chat_caption`, ~1.6 min cold): CRG_ClaimNest on one game (input_lock=1), F9, one
  click on the chat line, `text`, Send: the line in both chats; then without a caption, typed once.
## Cell mirrors that outlive the host's cell, plants and rocks per camera, the host's real evolution (2026-11-03, fix/w40-cell-evo)
<a id="testing-world-lod"></a>

* `coop_cell_gone.py` (BUG-0480, cell fixture, ~1 min warm): a guest's mirror is *stale* when the host's game
  had that cell index in neither of two dumps around the guest's dump (`coop.census all`, the first one 2.5 s
  before it). `--case lost` (suite `cell_gone`): the guest drops the host's MSG_CELL_STATE for 4 s
  (`coop.pool statehold <s>`) while the host destroys 8 cells near it (`coop.pool cull <r> <n>`, DestroyCell),
  then the state flows again: no stale mirror (0 in 6 rounds, out/lab/20261103-154618). `--case held` (suite
  `cell_gone_probe`): the state stays lost during the sample: the probe must see stale mirrors (its own FAIL
  case, out/lab/20261103-155103). `--case play` (suite `cell_gone_play`, extended): both swim, the host bites;
  0 stale in 4 of 6 runs, 49 meat mirrors at once in 2 cold runs (round 5 both times; the guest's mirror entry
  was refreshed 0.2 s before, the host's `coop.pool cell` and its census did not have the index:
  out/lab/20261103-155103, 20261103-155433; cause not found, BUG-0480).
  CORRECTION (2026-11-06, fix/w45-leftovers): those were host cells born after the first host dump and destroyed
  by the game's cull just before the second (docs/re/cell.md#cell-short-lived); a candidate now counts as stale
  only when the host's `coop.pool cell <key>` says it stopped sending it at least 1 s before the guest's dump
  (`last sent ... s ago`). `--case play` also fails on a host cell within 25 m of the guest (in both host dumps)
  that the guest has no mirror of.
* The detector's Cell compare (tools/tests/worlddiff.py `pair_dump`, `CELL_BRACKET`): the host is dumped before
  and after the guest, 1.5 s apart; a guest's proxy counts as `guest_only:cell` only when the host had its
  index in neither dump, a host cell as `host_only:cell` only when it had it in both. Cell cells live for
  seconds (food eaten, prey killed, a level change replacing the pool: 369 at once in out/lab/20261103-012624
  play_cell#2, host 397 rows / guest 1050), and one host dump next to one guest dump took a mirror on its way
  out (0.1 s, or 1 s after a lost chunk) for a stale one. `SPORE_DETECTOR_BRACKET=0`: one host dump (the old compare).
* `coop_world_lod.py` (suite `world_lod`, creature fixture, ~2 min, BUG-0473 / BUG-0498): the host stands, the
  guest walks away in 35 m steps to 200 m and back; both games' whole lists every step. Rocks: the 1 rock of the
  save is in both; 28 more appear in the guest's list only once it walks (30-75 m ahead of it, never in the
  host's, which stood 1 m from the guest at the start) and stay there. Plants (`game_plant`): 0-2 in a whole
  list, always 8-14 m from that game's own player, never in the other game (out/lab/20261103-161705). Both are
  made by each game around its own camera: `rock` and `game_plant` are in worlddiff.IGNORE. The test fails when
  an object one game has only lies near the other player (a real difference that player could walk to).
* `coop_editor_sync.py --case host` (suite `editor_sync_host`) and `--case host2` (`editor_sync_host2`, two
  evolutions in a row): the drag at y 170 never added a part in the host's evolution editor (the uncollected
  parts are locked there too); at y 780 it does, and the host's evolution is applied (DNA 40 -> 15 after the
  editor, the avatar's key 065CBE70 -> 29141F13, then 29141FE7 -> 29141FEC). The DNA shown during the editor is
  the editor's own budget (40 -> 40 in the log even after the part). The check: the guest's puppet species is
  the host's bundle key there (`player 0 creature:` of `coop.creations`, a content key, new per version) or
  its world key, with the host's creature's name. `coop.creations profile <hex>` prints the species profile the
  game has for a creature key (pointer, +0x504 key, +0x53C version).
* Pitfall met: a new test file named like an existing one (`coop_cell_stale.py` is BUG-0306's) replaces it
  silently with Write: check `ls tools/tests/<name>` first.
## NPC animations on the guest: npc_anims (2026-11-03, fix/w40-npc-anims)
<a id="testing-npc-anims"></a>

* `npc_anims` (coop_npc_anims.py, ~1.7 min cold, suite full): 40 s of the NPCs' own behaviour (no command), both
  games sampled twice a second with `coop.npc cur` (GetCurrentAnimation per NPC id); a sample matches when the
  proxy plays the host NPC's animation within 1.2 s; >= 60 %. Then a fight: the guest hits an NPC of another species
  (`coop.npc hit`) until it is in its attack node against the puppet; 15 s sampled, the proxy shows the attack
  animations (>= 60 %, at least one attack animation). Prints the miss pairs (host's, guest's) and `coop.npc anims`
  of both games (seen, idle, sent, played, snapshot counters). `SPORE_FIX_OFF=npc.anims` fails (15-22 %, fight 0 %);
  on: 79-82 %, fight 83 %. `coop.npc animquiet <s>` (guest): how long the snapshot waits after a start (1.5).
## Puppets freed by the game, guest combat (2026-11-03, fix/w40-guest-combat)
<a id="testing-guest-combat"></a>

* `puppet_freed` (coop_puppet_freed.py, ~1 min warm, BUG-0507): the guest toggles fullscreen twice (two device
  Resets), then its game destroys the host's puppet itself 3 times (`coop.puppet gamekill`: DestroyInstance, none
  of the mod's bookkeeping); `coop.status` `puppet_listed <id> <0|1>` says whether the puppet pointer the mod holds
  is a live creature in the game's object list. `SPORE_FIX_OFF=creature.puppet_alive` fails (the dead pointer is
  kept, listed=0).
* `pack_target` (coop_creature_pack_target.py, ~1 min warm, BUG-0512): the host recruits a pack member, clicks the
  guest's creature (the avatar's +0xE7C; the pack did not follow in the lab), then `coop.hp packtarget 1 combatant`
  gives every pack member the puppet as target (SetTarget with the combatant); the host's game must hold no pack
  member aimed at the puppet 0.5 s later. `SPORE_FIX_OFF=npc.pack_spares_partners` fails (16 of 16 samples).
* `cell_ko_bite` (coop_cell_ko_bite.py, ~1.5 min, BUG-0509): the guest (then the host) is knocked out as in
  cell_ko_food and bites the nearest creature 5 times through the game's Bite (`coop.pool realbite`, the first
  bite names it: `bit c<host key>`); the creature's hp in the host's game (`coop.pool cell c<key>`) must hold.
  `SPORE_FIX_OFF=cell.ko_no_bite` fails (killed). The host's refusal of a knocked-out guest's MSG_CELL_HIT is not
  reached by it (the guest blocks first).
* `puppet_deep` (coop_puppet_walk.py with `coop.track lift -6`, BUG-0513): the walking guest's states 6 m under the
  host's ground; `SPORE_FIX_OFF=creature.puppet_deep` fails (144 frames under). Standing, the game keeps a puppet on
  its ground by itself: only a walk shows it.
* `partner_silent` (coop_partner_silent.py, ~1 min, BUG-0513): `frame.limit 9000` stalls the guest for 16 s; the host's
  toasts (`coop.status` `toast ... '<text>'`) must say it is not responding, then responding again.
  `SPORE_FIX_OFF=core.partner_silent` fails. The test allows the hitch gate and the monitor's "hidden" rule (made on
  purpose).

## The host's editor without a reload (2026-11-03, fix/w40-no-reload)
<a id="testing-evolve-in-place"></a>

* `coop_evolve_in_place.py --case cell` (suite `evolve_in_place_cell`, cell fixture, ~2.5 min) and `--case creature`
  (`evolve_in_place_creature`, creature fixture, ~2.5 min), BUG-0508: the guest has no own creations. Cell: the
  host's mating call, the vote, an eye moved, done. Creature: +40 DNA, `coop.vote evolve`, the unlocked mouth at
  y 780 (#testing-world-lod's note), the guest's red cross. Then every guest mode 0.5 s apart for 60 s after the host
  is back: no GalaxyGameEntry / LoadGame, the host's `world saves ... for shares` unchanged, the guest's puppet =
  the host's newest bundle with its name, the guest's own cell model / avatar key = the host's new key, Creature:
  `world player species` (coop.creations, cSpeciesManager +0xA4) the same on both. `SPORE_FIX_OFF=world.evolve_in_place`
  fails (the reload: menu 3-4 s). `coop.status` "evolve in place: kept / sent after all / saves", `coop.creations`
  "follow" and "world player species" lines.
* Pitfall met: `on_mode_change` runs before `g_adapter` / `g_world` follow the new mode: code there that needs the
  stage's adapter takes `modes::for_mode(to)`.

## The party's DNA pace: dna_pace, dna_pace_cell (2026-11-03, fix/w40-dna-social)
<a id="testing-dna-pace"></a>

* `coop_dna_pace.py [--cell]` (BUG-0510, docs/re/creature.md#creature-dna-pace): both players earn 20
  through the game's reward function (`coop.dna gain 20`, `coop.pool meal 20`), checked on both games'
  own value: +20 with the pace, a guest's spend of 10 whole, `coop.dna pace 0` on the host +40.
  `SPORE_FIX_OFF=core.dna_pace` fails (+40). ~70 s each, cold.
* Test setups keep exact amounts: `coop.dna add N` and `coop.pool food N` are NOT scaled by the pace
  (`dna_pace::g_bypass`); use `coop.dna gain N` / `coop.pool meal N [plant]` for a gain as the game
  makes it. Real kills / social wins / meals in other tests are scaled (N = 2 in a two-player test):
  a test that expects a kill's full DNA reward on the shared total must read the divisor
  (`coop.status` line `dna pace: div=...`).
* `coop_creature_social_bar.py` (suite `creature_social_bar`, BUG-0518): the guest engages a Kook by real
  input and clicks the social ability's button (550,1020 on the 1440x1080 window, the first green slot),
  never a key; the host's Kook impressed, the guest's proxy impressed, the species table +1 on both.
  `SPORE_FIX_OFF=social.bar_click` fails. ~65 s cold (warm in the suite).

## The settings window without My Spore Creations: save_areas (2026-11-03, fix/w40-misc)
<a id="testing-save-areas"></a>

`coop_save_areas.py` (suite `save_areas`, ~1 min, one game: the lane's guest instance) replaces the guest
profile's `Documents\My Spore Creations` with a plain file, starts the game, waits for the galaxy menu and its
intro cinematic (`(menu intro)` in coop.status, the buttons are hidden until it ends; 900 frames were not
enough), then clicks the Spore button (16,1062) and Settings (660,560) in the 1440x1080 window. It checks `exc`,
the three save areas (`call Resource::GetDatabaseByID cdecl 0x11AC197`) and the window manager's modal window;
the folder is put back at the end. BUG-0506: SPORE_FIX_OFF=game.save_areas FAILS (fault at 0x6025A9).

## Cell level changes on two games: cell_grow (2026-11-03, fix/w40-misc)
<a id="testing-cell-grow"></a>

`coop_cell_grow.py` (suite `cell_grow`, cell fixture, ~70 s cold): the host's food goes up (`coop.pool food 30`
until `level=` changes) twice; during each level change the test compares, every 0.3 s, the host's cells with their
mirrors on the guest (coop.census, matched by the host's cell index) in the guest's metres (each position times
its game's `world_scale=` from coop.pool, read before and after the dump). Fails when the p90 error of a sample is
2 m or more. BUG-0511: SPORE_FIX_OFF=cell.world_scale gives 14-18 m (FAILS), the fix 0.1 m.

## Save areas at startup and in the editor: save_areas_editor, fixes_off.txt (2026-11-03, fix/w40-misc)
<a id="testing-save-areas-editor"></a>

* `coop_save_areas_editor.py` (suite `save_areas_editor`, cell fixture, ~70 s): the guest profile's
  `Documents\My Spore Creations` becomes a plain file before the games start; the guest's game must have the three
  save areas, its own mating call opens its Cell editor and it must not fault there (0x633765, GitHub #190).
* `game.save_areas` acts while the game starts, before a console can switch it: a profile file
  `<AppData>\Spore\SporeCoop\fixes_off.txt` (one fix name per line, `lane.GUEST_DIR + '/SporeCoop'`) switches fixes off
  when coop.dll loads (log: `fix <name> switched off at startup`). `save_areas` and `save_areas_editor` write it for
  SPORE_FIX_OFF=game.save_areas and remove it at the end. `save_areas` now also reads the game's log line
  `save areas after the game's startup` to show that the game's own startup left the areas out.
## A failed game start on purpose (2026-11-03, fix/w40-gh-issues)
<a id="testing-initfail"></a>

* `kit_initfail.py` (BUG-0521): a file `SporebinEP1\mods\test-initfail-<instance>` is read and removed by
  SporeKit's App::cSporeApp::Init hook, which then returns 0 without calling the game's Init: the game ends with
  ExitProcess(0) at frame 1, as on the player's PC. `off` in the file switches `kit.fix initfail` off from the
  start (the twin run; `SPORE_FIX_OFF=initfail` in the test writes it). ~25 s on one lane.
* Pitfall: returning 0 **after** the game's full Init does not work for this: the game's teardown then faults
  (0xC0000005 in MSVCR90 / steam_api frames), a crash report instead of the clean exit.

## Tribe actions of 0.7.5: tools (2026-11-03, fix/w41-tribe-actions)
<a id="testing-tribe-tools"></a>

* `coop_tribe_tools.py [--nofix] [--type 1] [--dist 9]` (suite `tribe_tools`, ~1.5 min cold): `coop.tribe build
  <type> [m]` (host, test setup: the 'tribe' fixture's palette is locked) makes a tool of the player tribe as
  the palette's placement does, `m` metres from the village centre; `coop.tribe tools` lists the player tribe's
  tools (`tool <i> <ptr> serial= type= pos=`) and what each member holds (`held <id> owner= tool=`, the game's
  +0x102C); `coop.tribe grab <member id> <tool index>` gives the member the GrabTool order a click on the tool
  gives (DoActionForObjects: a guest sends it to the host). `coop.wobj` prints `made=` for the kind
  `tribe_tool` (copies of host tools a guest made). `--nofix` = `SPORE_FIX_OFF=wobj.tribe_tool,tribe.tool_hold`.
* The fixture's player tribe has one tool, the fire pit (type 10), on both games; the rack (type 1) comes
  out on the village's edge 9 m off the centre. The host's member took the weapon 3-37 s after the order.
* `coop_tribe_epic_damage.py [--nofix] [--min-blow 30]` (suite `tribe_epic_damage`, ~1.5 min cold): `coop.tribe epic`
  (host) spawns the co-op epic 60 m off, `coop.tribe epic near <member id>` puts it 6 m from a member (repeated: it
  walks off), `coop.tribe epic stats` lists every live epic (`ours=`, hp, size, its herd's archetype and
  multipliers, `blows= last= max=`: its blows on members, counted in the host's citizen TakeDamage hook). The
  fixture's own epic does not hit anybody in the test (it is far off). Two blows come within 10-40 s.
## Tribe: a guest's saves and own tribes (2026-11-03, fix/w41-tribe-world)
<a id="testing-tribe-guest-world"></a>

* `coop_tribe_save_strip.py` (suite `tribe_save_strip`, ~2 min, the free lane for Carol): Carol joins the
  host's tribe world, the host adds 4 adults (`coop.tribe spawn 4`) and forms an NPC tribe 70 m away
  (`coop.tribe npc_tribe 3 70`); Carol saves (`coop.save`), restarts alone and loads: her player tribe holds her
  own members only and no tribe stands where the made one did (`coop.tribe` marks `made=1` tribes and `ours=1`
  members on a guest). `SPORE_FIX_OFF=tribe.save_strip` fails. The host's game forms more NPC tribes of its own
  while the test runs (2 more in 2 min on the tribe fixture): compare tribes by place, not by count.
* `coop_tribe_guest_tribes.py` (suite `tribe_guest_tribes`, cold, ~2 min): `coop.tribe form` on the guest runs
  its game's NPC tribe controller's forming (NpcTribeController_FormTribe, what the game does during play); then
  with the fix off for a moment a tribe the host has not is made and must lose its members once the fix is on.
  `SPORE_FIX_OFF=tribe.guest_tribes` fails.
## Civ roads and citizens (2026-11-04, fix/w41-civ-citizens)
<a id="testing-civ-roads-citizens"></a>

* `coop_civ_roads.py` (suite `civ_roads`, warm, about 50 s): the host's game adds a trade route (`coop.civ roads 0 1`,
  the game's AddTradeRoute); 8 s later the guest has as many road objects (`coop.civ nouns 2B8A4E7`) and both cities
  list the route. Twin: `SPORE_FIX_OFF=civ.roads` (guest: no road).
* `coop_civ_citizens.py` (suite `civ_citizens`, warm, about 100 s): 8 rounds 6 s apart, the citizens of both games
  around the same centre (`coop.census all`): at most 10 % of the host's without a partner within 15 m and at most
  10 % of the unambiguous pairs (each the other's nearest within 3 m, the next 2 m farther) of another size.
  Twins: `SPORE_FIX_OFF=civ.citizens` (about half unpaired), `civ.citizen_scale` (sizes differ).
* Pitfall met: a lab run with `SPORE_FIX_OFF` leaves the switch off in the warm games; the next run without it on
  the same lane reused them (`fix=0` in `coop.wobj`). Run the PASS after a twin with `--cold`.

## Civ shots between the games (2026-11-04, fix/w41-civ-combat)
<a id="testing-civ-shots"></a>

* `coop_civ_shots.py` (suite `civ_shots`, warm, about 110 s): the host's military vehicles attack the nearest NPC
  city (`coop.civ attack <id> <city> 2`: the game's SetOrder); 60 samples 1 s apart of the projectile objects in
  each game's noun list (`coop.civ nouns 24270C5` and the other projectile nouns). PASS: the host fired, the
  guest had projectiles in at least half the host's samples, at most about twice as many, and the city's
  building hit points on the guest did not fall (its copies of the shots hurt nothing). Twins:
  `SPORE_FIX_OFF=civ.shots` (no projectile on the guest), `civ.shot_damage` (the guest's city loses hit points).
  The status line `shots:` (`coop.civ`) counts the host's FireTool calls seen, the guest's calls, the targets
  found and the hits dropped.
* `sync_civ_loss` (coop_sync.py civ under SPORE_NET=burst, cold, about 1 min): the sync_burst variant for Civ
  alone, for BUG-0438. With one vehicle sampled 16 times its p90 is the second worst sample: one burst decides
  a run (see the ticket).
## Creature leftovers of 0.7.5 (2026-11-03, fix/w41-creature-left)
<a id="testing-k1-creature"></a>

* `creature_social_meter` (coop_creature_social_meter.py, ~1.2 min warm): the guest's social game with a Kook by
  real input; `coop.social meter` prints the game's meter object (`visible=` the layout's own byte,
  `visible_at_win=`, `rounds=`, `stage_win_msgs=` the 0x60B4123 the creature-stage handler got). The Kook needs one
  round: the win usually comes within the engagement's first second, so the test reads `visible_at_win`, not a
  live sample. The meter's bar fields stay 0.0 on the guest (not laid out in that time): no bar check.
* `chat_modal` (coop_chat_modal.py, ~1 min warm): the host's "game saved" box held up with `coop.save hold`
  (`coop.save box` says whether it is up, `coop.save confirm` lets the mod confirm it); `coop.uistate` has
  `modal=` (the window manager's modal window) next to `input_lock=`.
* `nest_heal` (coop_nest_heal.py, ~1.7 min): each player at the nest with `coop.team off` (the pack regeneration
  would heal too) and `coop.team hp 3`, 12 s; then 100 m away. Both heal at the fixture's nest (BUG-0496 not
  reproduced).
* `evolve_key` (coop_evolve_in_place.py --case creature --nosave, ~2.5 min): `coop.evokey nosave` turns the host's
  in-place save off (a host alone does not save either); `coop.evokey` prints `keys_set=`.

## Puppet look: jumps, nest mound, stops, the gait near the camera (2026-11-04, fix/w41-puppet-look)
<a id="testing-puppet-look"></a>

* `coop_puppet_walk.py --air` (suite `puppet_air`, warm, ~1.5 min: `--dir guest --jumps 0.9 --secs 30`, hops by
  `coop.abilities lift 1.5`): of the frames with the walker's target more than 0.4 m over the observer's ground, the
  next frame draws the puppet at least half as high in >= 80 % (at least 5 hops). With `--air` a drawn frame step that
  follows a step of the target itself (the hop is a teleport of the walker's own creature) is no jump of the driving.
  `SPORE_FIX_OFF=creature.puppet_air` fails (5 %; with the fix 99 %).
* `--mound x,y,z` (suite `puppet_mound`, cold: `--goto`): the walker stands 10 s 0.5 m over that point (it lands on
  the fixture's nest mound, lower part at -128.4,-487.5,-67.4), then walks; checks that of the frames with the target
  over 0.15 m above GetHeightAt and the puppet on the screen at most 10 % are drawn 0.15 m under the walker's height.
  `SPORE_FIX_OFF=creature.puppet_mound` fails (179 of 303 frames).
* `--stop-walk`: walks of 1-2.5 s by W, each followed by a 1.4 s stop, 60 % of them abrupt (`coop.abilities halt`:
  StopMovement, a teleport in place, velocity 0); prints per stop the drawn way after the stop less the target's
  (`slid on ...`) and checks at most 10 % over 0.15 m. Not in the suite: no stop slid with the 0.7.4 driving (BUG-0408).
* `--zoom N`: both cameras zoomed out by N wheel turns (`wheel x y -5`); the wheel stops at 25-60 px/m.
* `coop.track` column 29 `ppm`: pixels per metre of the creature on this game's screen (0 off screen). puppet_walk
  judges the walk gait only where it is >= 10 (BUG-0436: three gait-0 runs had ~1 px/m, the camera hundreds of m
  away) and prints `NOT MEASURED ... the observer's camera is far away` when most frames are farther.
* `coop.track air <v>` (tests): the puppet in the air 0 placed and marked not grounded (the fix), 1 placed only, 2
  teleported, 3 walked on (walk_place).
## Cell leftovers of 0.7.5: the camera, the partner in a level change, Cell twins (2026-11-04, fix/w41-cell-left)
<a id="testing-l1-cell"></a>

* `cell_camera` (coop_cell_camera.py, cell fixture, ~1 min warm): waits for the hatch to end (`coop.uistate`
  `input_lock=0` and `cine=0`: during the hatch the game puts the cell back at its start, a jump made then is
  undone); the guest `coop.pool tp 40 0`, then the host `coop.pool selfhit 250` with the guest 40 m away (it
  respawns next to the guest after 10 s). `coop.pool cam` prints the camera (`off=` the drawn point to the own
  cell). Fails when `off` is 1.5 m or more 3 s after each jump. BUG-0343: SPORE_FIX_OFF=cell.camera_snap 7.0 m
  (FAILS, lab 20261104-020658), the fix 0.1-0.2 m (lab 20261104-020550).
* `cell_puppet_scale` (coop_cell_puppet_scale.py, ~70 s): both cells idle 7 m apart, the host's food up until two
  level changes; every 0.25 s `coop.pool scalepos` on both (scale, own cell, puppets in one frame); the error is
  the puppet against the partner's own cell, both times their game's scale, in the drawing game's metres.
  BUG-0519: SPORE_FIX_OFF=cell.puppet_scale p90 8.0 / 4.2 m (FAILS, lab 20261104-021951), the fix 0.5 / 0.3 m.
* `parts_cell_twin` (coop_parts_shared.py --from-cell --guest-cell-part, ~2.5 min cold): the guest collects a Cell
  part with a creature twin that neither game has (`coop.parts cell collect <i>`), then Cell -> Creature the
  players' way; the twin must be IsUnlocked (`coop.parts states`, the `u`) on both games, then the test stops.
  BUG-0446: SPORE_FIX_OFF=parts.shared_cell FAILS (lab 20261104-022323), on PASS (lab 20261104-022114).
## Tribe: orders and the grab spots (2026-11-04, fix/w41-tribe-orders)
<a id="testing-tribe-orders"></a>

* `coop.tribe ai <member id>` (any game; the host's is the one that counts): the member's order record (`rec=`
  action, `target=`, `has=`), behaviour node (`node=`, `depth=`), the leaf node's memory (`mem=` state, the tool,
  the tool's position at the enter, flags, claimed spot, wait counter for the grab node 0x0462854D), the walk
  (`mode=`, `flags=`, `goal=`, `r=`) and `sel=`. `coop.tribe layout <tool type>` (host): the tribe's grab spots of
  that type (place, `used=`, the member on it). `coop.tribe put <id> <x> <y> <z>` (host): test setup, a member onto
  the ground there. docs/re/tribe.md#tribe-orders.
* `coop_tribe_tools.py`: the guest's member walks to one of the rack's grab spots (a ring ~7 m round it) and,
  in some runs, stops 1.5 m short of it for good (3 of 8 runs before the fix). Twin: `SPORE_FIX_OFF=tribe.grab_spot`
  over `--repeat` (the failure is not deterministic: putting the member next to the fire pit spot, or another
  member onto the spot, with `coop.tribe put` did not make it stop: it reached the spot or took another one).
  `--own`: the host orders one of its own members (not the chief: tool 11 does not grab) the same way, a control.
* play_tribe logs `host ai #<id>: ...` for every member of an order nobody acted on.
* `creations` (coop_creations.py, ~1 min, BUG-0345 since 2026-11-04): the guest's own creature by the per-world
  rule: `coop.creations unchoose creature` + `worlds forget` (a world it never played its creature in) -> the
  world's species; `choose` -> own, the notice "plays its own creature" in both game logs, `worlds` records it;
  unchoose (same world) -> still own; forget -> the world's species; the window's next. The host knows the
  guest's creature under its shared remapped key (7C...), not the guest's id: compare the host's puppet with
  "not the world's species". SPORE_FIX_OFF=creations.guest_world_species
  FAILS (lab 20261104-033933).
## Watchdog, world transfer, world events (2026-11-04, fix/w41-infra)
<a id="testing-g1-infra"></a>

* `kit_watchdog.py` (BUG-0347, one game, ~2 min): `kit.test.window minimize` for 40 s, then `kit.test.hang 40000`.
  Minimized under Wine the frames stop (IsIconic 1, 66 -> 66) while App::Canvas::Tick goes on. `SPORE_FIX_OFF=hangloop`
  (or `--old`) fails the minimize case (the WATCHDOG dump). Pitfall: a hidden window (`kit.test.window hide`,
  SW_HIDE) is no pause under Wine: the game goes on presenting without the frame cap (~80 000 frames/s, 440 ->
  84 282 in 4 s), so that case is only run with `--case hide`.
* World transfer statistics: the host logs one line per transfer when the guest has acknowledged all of it,
  `world: transfer to N acknowledged (KB in s, KB/s; packets P, resends R = x %, rtt ms)`. The fix's switch
  `net.bulk_acks` (BUG-0405); measure with `rejoin_race_long` (clean loopback, the guest loads while the world
  comes) and `rejoin_mobile` (SPORE_NET=mobile), each twice (`SPORE_FIX_OFF=net.bulk_acks` and on). On the clean
  link: 27 transfers 63 % resends (160 228 of 255 858 packets), median 2.2 s / 1571 KB/s (off,
  out/lab/20261104-032411) against 33 transfers 0 resends of 117 813 packets, median 1.2 s / 2939 KB/s (on,
  out/lab/20261104-033012). net_tests `bulk_busy_receiver` simulates the same (a guest that reads once per 200-950
  ms frame, 256 packets per update) on clean, 40 ms / 3 % and 150 ms / 10 % links.
* `coop_drama_guest.py` (BUG-0486, suite drama_guest, ~1 min warm start): `coop.drama start 2` (the Drama Manager's
  own StartKind for the UFO visit) on the guest and then on the host; the UFOs are counted in each game's object
  list (`coop.census` nouns, worlddiff.NOUNS 'ufo'). `SPORE_FIX_OFF=creature.guest_events` fails (the guest's UFO).
* CORRECTION (2026-11-04, fix/w41-cell-left): coop_creations.py no longer leaves the fixture world recorded (that made
  later tests depend on the order and on the lane). Tests that need the guest's own creature choose it and put the
  guest's choice and this world's record back (`tools/tests/creations_state.py`: save / choose_own / restore; used by
  creations, creations_rejoin, old_species), and wait until the host has the guest's own creature rather than the
  world's species it announces first. Verified in both orders, --cold: creations, creations_cell, creations_rejoin,
  old_species, npc_swap, creation_leave OK (lab 20261104-053801 .. 20261104-060316); legless_guest fails either way
  (BUG-0533, not the rule).
## Tribe tools of every tribe, a guest's purchase, orders during a gather (2026-11-04, fix/w42-tribe-left)
<a id="testing-tribe-tools-all"></a>

* `coop_tribe_tools_all.py` (suite `tribe_tools_all`, BUG-0523): the host forms an NPC tribe (`coop.tribe npc_tribe 3
  70`) and places a rack for it (`coop.tribe place <host tribe index> <type> <m> [deg]`, any role: the index is the
  host's MSG_TRIBE_STATE order, a guest maps it); the guest places an own tool of that tribe. The guest's tribes' own
  tool lists (`coop.tribe tools all`) must match the host's within 3 m near the village. The new tribe's index is
  looked up by its fire pit: an added tribe can shift the indexes of the object list. ~60 s warm.
* `coop_tribe_tool_buy.py` (suite `tribe_tool_buy`, BUG-0524): `coop.tribe buy <type> <m> [deg]` on the guest runs
  the palette's placement calls (the old tool of the type goes, CreateTool, dragged: spatial +0x72) until `coop.tribe
  buy_end`; the host's tool list must have it within 2 m after the drag only, and a guest's member takes the weapon.
  ~65-90 s.
* `coop_tribe_gather_stall.py` (suite `tribe_gather_stall`, BUG-0531 regression check): `coop.tribe order <id>
  <action> <x y z>` (host) sends a guest's member to gather a tree 90-130 m off; the guest's walk order (select +
  walk) is given while it carries the food home (gather state 19). Pitfall: the game's walk ends some metres short
  of goals near the hut: the test checks the member leaves the gather node and walks 5 m of 15 toward the goal.

## Life on a colony planet in Space: space_planet_citizens, space_planet_vehicles, space_colony_spice (2026-11-04, fix/w42-space-planets)
<a id="testing-space-planet"></a>

`tools/tests/coop_space_planet.py --case citizens|vehicles|spice` (space fixture, 3-5 min each;
docs/re/space.md#space-planet-life). Each asserts the game's own objects in the other player's game:

* `citizens` (BUG-0490): both ships land on the home planet, the host's ship is put over the guest's
  (`coop.space over x y z`: on a planet, the own ship over a point at its height), 12 s for the host's game to
  move its citizens there; 3 rounds of `coop.census all` within 400 m of the guest's ground point: every host
  citizen needs a guest citizen within 3 m and of the same size (12 %); FAIL over 5 %. Twin
  `SPORE_FIX_OFF=space.citizens`.
* `vehicles` (BUG-0490): the colony's 40 vehicles, 4 rounds: a host vehicle without a guest vehicle within 15 m
  counts; FAIL over 10 % (the two dumps are a fraction of a second apart at ~20 m/s). Twin `space.vehicles`.
* `spice` (BUG-0537): the host lands (9 pickups by its cities), the guest jumps to the next star and back (its
  game collects the home colony's stored spice into its hold on arrival, as every arrival does); 10 s later the
  host's colony cities keep no more than the guest's, the host's planet shows at most 1 pickup, the host's hold
  is unchanged, and after the guest lands both planets show the same pickups. Twin `space.colony_spice`.
* `coop.space colonyspice`: the feature's counters and this game's colonies with the stored spice of each city;
  `coop.wobj` lines `space_citizen` / `space_vehicle` and `space vehicles drives=`.
* The divergence detector in Space (worlddiff.py): citizens near the guest are compared only when the host's
  game has citizens there (each game puts its pool of 150 at the cities near its own camera); a ship's size is
  compared relative to the own ship of the game that draws it (each game sizes every ship by its camera zoom).

## Space lifecycle tests: saves and rejoins (2026-11-04, fix/w42-space-life)
<a id="testing-space-lifecycle"></a>

* `coop_space_save_strip.py` (~2 min, needs the free lane): Carol (lane B) saves in the host's star system
  with two puppets and a mirror, is restarted alone and loads the save; our ships are told apart in
  `coop.fleet list` by `puppet` / the mirror id, `inv=` and `model=`, and in the reloaded world by type, model
  and the spot they stood on (5 m). The game does not keep the invincible byte: a reloaded puppet is a plain
  Defender (`inv=0`), so "no invincible Defender" alone does not catch it.
* `coop_space_colony_rejoin.py` (~1 min): pitfall: right after `coop.leave` and a join the guest still answers
  `coop.avatar` in its own old world, so `twoinst.wait_guest_in` can return before the host's world is loaded;
  wait for `coop.space world` (`world=N open=1`) to move on first.
* Console answers were cut at 8 KB until 2026-11-04 (SporeKit `out_printf`, BUG-0388): a dump read in pages of
  300 rows got ~97 of each page, silently. Now up to 1 MB per printf (`kit.fix longout`); `coop_space_drawn.dump`
  prints `track dump: N of M rows read` when rows are missing. A test that reads a long answer should still count
  what it got against what the game says it has.
* `SPORE_FIX_OFF=kit.<name>` switches a SporeKit fix (`kit.fix <name> 0`) off on both games (twoinst.start).

## Creature leftovers of 0.7.6 (2026-11-04, fix/w42-creature-left)
<a id="testing-creature-leftovers-076"></a>

* `creature_social_prompts` (coop_creature_social_prompts.py, ~70 s warm, BUG-0525): a real engagement of a Kook, then
  the answers as the key / bar path sends them (`coop.social act <id> 0`, `coop.social answer <id> <hex type>` for an
  ability of another kind). The prompt's animation is checked on the guest's mirror with `coop.npc cur` (the game may
  play another of the animation's family: 042A1CAF as 042A1CB3). A failed social game leaves the guest not engaged; the
  test engages again with `coop.social act` (the click's camera sweep found the Kook again in 1 of 2 runs).
* `puppet_air` and BUG-0532: the 1-3 frame drops come in about 1 run of 3; a FAIL / PASS claim needs a series
  (`--repeat 3 --retry 0` on, `--repeat 4` off). `coop.track air` prints the air hold's counters.
* `PUPPET_JUMP_WORLD=1 coop_puppet_jump.py` (BUG-0536) runs it with the world's species; a lane whose guest profile
  recorded its own creature for the fixture world plays its own creature anyway: the first run clears the record and
  fails, the next `--cold` run plays the world's species. Put the record back afterwards (`coop.creations worlds set
  creature <key>`), other tests choose the own creature themselves.
## The effects relay's cost check: effects_party (2026-11-04, fix/w42-infra)
<a id="testing-effects-cost"></a>

BUG-0527. `coop_effects_party.py` checks that each of the three games spends under 1 % of its main thread in the
relay while all three create 2.5 effects a second (`coop.effects load 1 400 <id>`). The check before measured the
whole `effects::tick` in wall time (`effects cost: tick avg ... over N frames`) and failed on a loaded machine (lane 4,
load average 8-12: 1.0-5.4 %, also with the 0.7.4 build). `coop.effects` now prints the tick's parts
(`effects cost parts: load / rx / live / rest; relay ... in N frames: under 5 ms ..., slow N frames ...`), measured:

* the workload itself (`load`: the game's CreateEffect at the player, what a player's own game does anyway) is
  0.2-3.2 % of the main thread under load: it is no relay cost and is left out;
* ticks over 5 ms (29-35 frames in 20 s at load average 10-12, 0-10 at 8-11, 0 at 6) held 2.6-5.3 % of the wall
  time but 0-5 jiffies of the thread's CPU time (GetThreadTimes, a build of 20261104-122037): the OS preempted them;
* the relay's own cost over the frames under 5 ms is 0.42-0.58 % (lab 20261104-123812; most of it `live_tick`,
  0.25-0.56 %, the release bookkeeping of relayed effects; the hooks 0.0002 %).

The check is now: hooks + the relay part over frames under 5 ms < 1 %, and frames with a relay part of 5 ms or more
< 5 % of the frames (a relay that is slow every frame still fails). `--old-cost` runs the old check (FAIL on lane 4:
the worktree's out/effects_party_tribe_oldcost_20261104.log, 2.3-3.0 %; in the passing run 20261104-123812 the old
numbers were 1.27 % (Alice, Tribe), 1.21 % (Carol, Tribe) and 1.00 % (Alice, Civ)).
* Pitfalls: the thread's CPU time is no measure under Wine (GetThreadTimes moves in 10 ms steps and costs ~50 us
  a call: called every frame it was itself 0.4 % of the main thread). The old number (`tick avg` x frames) also
  restarts every 60 s with the `perf:` line, so a window over that moment was wrong.

## Space play tests: session ends, own systems, diplomacy away, the disable rule after a load (2026-11-04, fix/w42-space-play)
<a id="testing-space-play-w42"></a>

* `coop_space_trade_end.py` (~1 min warm): `coop.space trade hold 2` makes the receiver keep a gift or a sale without
  answering (its answer "lost"); `trade` shows `delivered=N` (gifts the partner's side acknowledged at the transport)
  and `kept_at_end=N`. `hold 1` (no add, no answer) is the old switch for the "still on its way" case.
* `coop_space_allies_own.py` (~1 min warm): the guest founds a record-level colony in the fixture's home system
  (`coop.space allies colonize <PlanetID>`); the host's `allies planet <PlanetID>` must show tech 5 and a cCivData of
  the host's pol. `allies star` lists `loaded(pol=...)`: the political id of the loaded cPlanet. A run of this kind
  must show 0 faults: a record-level colony the game's HUD does not expect faults at 0x107453A (handled, counted).
* `coop_space_diplo_away.py` (~2 min, fixture `space_empire`): `coop.space away 1` on the host stands for an editor
  entered from Space; the check waits 25 s after the host is back so the host's periodic table (every 20 s) has run.
* `coop_space_down_reload.py` (~1 min warm): calls `twoinst.warm_reset` itself inside one test, so a state the
  lab's warm reset carries over (BUG-0257) shows in a single run; it skips when the reset takes over 58 s.
* BUG-0257 pitfall: tests that leave a ship disabled change the next warm test on the same games for 60 s; since
  `space.down_epoch` a load ends that, but other per-world state may still carry over a warm reset.

## A Sporepedia cache without ship models: space_npc_model (2026-11-04, fix/w42-infra)
<a id="testing-space-npc-model"></a>

BUG-0546. `coop_space_npc_model.py --solo` (suite `space_npc_model`; `space_npc_model_coop` with a guest) copies
lane 5's Sporepedia cache of 2026-11-04 (no UFO with its model record) over the host profile's
Pollination.package, loads space_empire, flies to star 004A0014 and checks there is no fault; the lane's own file
comes back at the end. The cache copy is not in git: `out/fixtures/bug0546-Pollination.package` of the main
checkout (else `out/bug0546/lane5-Pollination.package` of the fix/w42-infra worktree); without it the test SKIPs.
It also SKIPs when the guard had no ship to fix (lane 4: its other caches have the models). About 1.5 minutes.
`SPORE_FIX_OFF=game.ufo_model` must FAIL (lane 5: out/lab/20261104-130233).

## The guest as a player: stun, flight, flock, nest ally, Tribe ring (2026-11-04, fix/w43-guest-player)
<a id="testing-player-rules"></a>

P1 of 0.7.7 (docs/re/creature.md#creature-player-rules). Each test checks the guest's own game's state.

| Test (suite) | What | Twin switch |
|---|---|---|
| coop_creature_stun.py (creature_stun) | the host's NPC plays its charge on the guest's puppet (`coop.rules charge <npc> <player>`: SetTarget + PlayAbility, the hit and the stun are the game's); the guest's avatar holds stimulus 0x1000; the guest's stun of a proxy stuns the host's NPC (`coop.rules stun <s> <id>`, as its own hit) | creature.stun_relay |
| coop_creature_air.py (creature_air) | a wild NPC bites the guest on the ground, then the guest's creature is kept 1.5 m up for 20 s (`coop.abilities lift`): no host melee hit lands while the guest's own game has it off the ground (`coop.hp dmglog`: `air=1`) | creature.air_immune |
| coop_creature_flock.py (creature_flock) | the host's game starts its Summon Flock event (`coop.drama start 8`); on the guest every flock proxy answers vt 0xD4 friendly and wears the hearts (`coop.rules friendly <id>`) | creature.party_allies, npc.flock_hearts |
| coop_creature_nest_ally.py (creature_nest_ally) | the host's record of a species becomes allied; the guest's herds of it get +0x191 (`coop.rules nests <inst> <type> <group>`), and lose it again | social.nest_ally |
| coop_tribe_target_ring.py (tribe_target_ring) | the guest selects its members and orders an attack (`coop.tribe act 7`); its HUD's ring map holds a live ring under the enemy (`coop.tribe ring <id>`, the HUD's own lookup) | tribe.target_ring |

* `coop.hp dmglog [clear]`: the last 48 hits through the creature TakeDamage hook (victim, attacker, amount, type,
  caller, filtered / applied / direct = the mod's own apply_damage, air = the victim not grounded). A bite of an NPC
  is type 2 from 0xC20A78.
* `coop.social abil <id>` prints each ability's effect list (`fx kind:chance ... stun=<s>`): the NPCs with a charge
  (type 0x1F) of the creature fixture are of 065CB811, 065CBF12, 065CB8BD and the host's family.
* Pitfall: walking the HUD's ring map by hand from the console hung a guest twice (the owner pointer taken from
  [0x169B2C8] +0x240 was not the map's); the test asks the game's own lookup on the owner the HUD's update ran on.
* Pitfall: dropping CreatureAttackTarget on an airborne puppet left a charging NPC flying on towards it (82 m up, a
  sync-check `n apart` in lab 20261104-212957): the hit runs, only its damage / stun are not carried.
## Host-owned world state tests (2026-11-04, fix/w43-world-state)
<a id="testing-wsync"></a>

`coop.wsync dump [row]` prints the game's own values of the world state rows (docs/sync-architecture-plan.md#sync-wsync);
tools/tests/wsync_util.py parses them and `differences(host, guest)` lists every row a guest's game has other than
the host's. Tests: tribe_rel / tribe_goal / wsync_stages (coop_wsync_tribe.py rel|goal|stages, 1-5 min),
wsync_transitions (the class test on Tribe: the host's game over and reload, a rejoin, the guest's own changes;
8 min), cell_editor_budget (the host's Cell editor through the vote, 3 min). Pitfall: a rejoin loads the host's
current world, so relations, the goal, the stages and the food are the host's after a rejoin even without the rows.
## Whole-planet re-alignment tests (2026-11-04, fix/w43-realign)
<a id="testing-realign"></a>

* `coop.realign planet [kind]`: every village, hut, tool, food mat, nest and city of the game's noun list (the
  whole planet, not 250 m) with its position, `fl=1` for the player's own and a hut's / tool's health in 1/1000;
  `coop.realign`: the counters and the last compare per kind (here / host / lack / extra / state_off / removed /
  state_fixed); `coop.realign hp <kind> x y z <permille>`: the health of the nearest hut / tool (tests).
* `coop_realign_planet.py --fixture tribe|creature --case gameover|rejoin` (suite `realign_tribe`,
  `realign_tribe_rejoin`, `realign_creature_rejoin`): a transition, then the two games' planets compared whole
  (villages, huts with their health, food mats, nests, cities; tools are printed, not counted: BUG-0488).
  `SPORE_FIX_OFF=realign.villages,realign.buildings` fails `realign_tribe` (the extra villages, huts, food mats
  stay; the host's damaged hut is not shown).
* `coop_tribe_epic_species.py` (`tribe_epic_species`, ~14 min): the live Creature -> Tribe change, then the co-op
  epic is no player's species (BUG-0563).
* Pitfall: the host's game-over caption in the tribe fixture comes ~20 s after the last member is out, later under
  load; while the host's own creature_attack event runs it may not come at all (BUG-0582). The bounce tests wait
  75 s for it and press its OK again when the game has not left the stage 10 s later.
## The detector's world state and whole-planet rows (2026-11-04, fix/w43-detector2)
<a id="testing-detector-state"></a>

The detector of [#testing-detector](#testing-detector) compared only objects within ~250 m of the guest. The 2026-11-04
player reports (BUG-0554..BUG-0579) were mostly world state it could not see. Now every sample also compares:

* `coop.wstate` (mods/coop/wstate.cpp, one command per game, read-only, all from the game; offsets in
  docs/re/tribe.md#tribe-detector-state and docs/re/creations.md#creations-cplayer-fields): the sky (time of day) and the
  day's length, cPlayer's consequence trait per stage (stages done) and the stage progress bar, the mode's shared resource
  (food / money), Cell food / spent DNA, Creature DNA and the species table (relation, impressed, killed per species), every
  tribe's food, members and relation to the player tribe, the tribe mission manager's current mission (the goal), every
  civilization's money, relation and cities, every NPC empire's relation and war / alliance flags, and the planet's whole
  lists of villages, huts, tools, totems, food mats, nests, cities, buildings and turrets with position and owner.
* `worlddiff.diff_state` / `diff_planet` (`pair_dump(..., state=True)`; the monitor's `SPORE_DETECTOR` samples use it,
  other callers of pair_dump do not), signatures and tolerances:

| signature | compared | tolerance | ticket |
|---|---|---|---|
| `All:state:time_of_day` | the planet context's orientation (what MSG_WORLD_TIME sends) | 20 deg | BUG-0579 |
| `All:state:day_length` | cTimeOfDay +0x24 | 2 % | BUG-0579 |
| `All:state:stage_done` | a stage done (trait != 0) on one game only | exact | BUG-0577 |
| `<stage>:state:goal_progress` | the stage progress bar (cPlayer +0x10F0 / +0x10F4) | 5 or 10 % | |
| `<stage>:state:money` / `food` | `coop.res`'s game value (Tribe: the player tribe row) | 20 or 5 % / 3 or 10 % | BUG-0572 (Tribe food) |
| `Cell:state:dna`, `dna_spent` | cCellSerializableData food, spent DNA | 3 or 10 %, 1 | BUG-0560 |
| `Creature:state:dna` | GetEvolutionPoints | 5 or 10 % | |
| `Creature:state:species_relation` / `_impressed` / `_killed` / `species_only` | the species table, common keys (a guest's 7C / 7D keys have no partner) | exact | BUG-0419 class |
| `Tribe:state:food`, `player_members` | the player tribe's bundles, members | 3 or 10 %, 1 | BUG-0572 |
| `Tribe:state:relation`, `members`, `tribe_food` | NPC tribes matched by centre (30 m): level by the manager's thresholds or 1.0 apart, members 1, food 5 or 20 % | | BUG-0565, BUG-0564 |
| `Tribe:state:goal` | the current mission's tribe (30 m) and state, or a goal on one game only | | BUG-0564 |
| `Civilization:state:relation`, `civ_money`, `cities`, `player_cities`, `civ_only` | civilizations by political id | 1.0, 50 or 20 %, exact | |
| `Space:state:relation`, `war_alliance`, `empire_only` | NPC empires by political id | 0.5, flags & 3 | |
| `<stage>:planet:<kind>` | the planet's whole list of a kind (village 25 m, city 25 m, others 8 m) | presence + position | BUG-0564 (village), BUG-0566 (tribe_hut), BUG-0574 (tools, totem) |
| `<stage>:planet_owner:<kind>` | matched objects: the player's on one game only, or one host owner that is two owners on the guest | | BUG-0565 (village) |

* No player position (an RTS game in the background): the state rows still count; the sample is no longer `nocentre`.
* `tools/detector_report.py` maps the new signatures in `KNOWN`; an `All` row's share is of all comparisons.
* `tools/tests/tools_selftest.py worldstate` (in `make test`) checks the rows on made-up dumps.
* First long runs on rel/0.7.7 = 0.7.6 code (labs 20261104-203017, 20261104-210208; out/detector of the report in the D0
  report): Tribe `state:tribe_food` 57 % (BUG-0580, new), `planet:tribe_tool` 41 % (BUG-0574), `state:relation` 34 %
  (-7 aggressive on the host, 0 neutral on the guest: BUG-0565), `state:goal` 28 % (BUG-0564), `planet:village` /
  `tribe_hut` 7 % (BUG-0564 / 0566), `All:state:time_of_day` 16 % (the host's sky stood still: BUG-0579); Creature
  `state:species_killed` / `species_relation` 1-2 %. The mission's +0x84 held pointer-like values: it is printed, not
  compared.
* First runtime check (lane 2, tribe fixture, quiet): both games equal (food 15 / 15, the NPC tribe 4 members, the same
  political ids, the sky 0.7 deg apart), 0 rows in 14 samples of `detect_tribe` (lab 20261104-201322). The tribe fixture
  has no stage done (it starts at Tribe) and no tribe-to-tribe mission (`goal tribe=-1`): those rows are not shown to fire
  on a real game yet.

## The long-session bot (2026-11-04, fix/w43-detector2)
<a id="testing-long-session"></a>

`tools/tests/coop_long_session.py --stage creature|tribe [--minutes 40] [--events death,editor,menu,restart,rejoin]`
(suite `detector`: `long_creature`, `long_tribe`, 40 min of play, about 55-65 min with the transitions, lab timeout
80 min; `long_<stage>_short`: 6 min of play, about 12 min). The play is the stage's play bot (play_creature.Player threads,
play_tribe.Game) in segments; between them one transition of the guest: `death` (Creature: `coop.team die`; Tribe: the
guest's chief leaves the tribe through `coop.tribe drop_chief 1`, an approximation: no real Tribe death of a player),
`editor` (the host's evolution vote, the guest's "I'm ready", the red X on the host and, in Creature, on the guest's own
editor too), `menu` (the guest's pause menu, Exit to Main Menu, Don't Save x2), `restart` (the guest leaves the session,
its game is SIGKILLed, started again and joins: a player closing the game; a crash without the leave made the host's
puppet rule fire, lab 20261104-210208), `rejoin` (`coop.leave` and join). 20 s after each one it prints a detector sample with
`"after": "<event>"` (DETECTOR-SAMPLE / DETECTOR lines like the monitor's); with `SPORE_DETECTOR=<s>` the monitor samples
during the play as well. It fails only on faults, crashes, freezes and a guest that is not back in the host's world
within 4 min of a transition (the session cannot go on); the divergences are rows for `tools/detector_report.py`.

* After every play segment an editor the play opened is left with the red X on both games (the creature bots' own
  evolution vote ends in the host's editor when its time is up: lab 20261104-210208, the rejoin then waited on it).
* Pitfalls met: in Creature the vote opens the guest's editor too (lab 20261104-203017: the guest stayed in its editor
  after the host's red X); a 40 min session exceeds the lab's default 30 min timeout (`timeout=4800` in suite.py).
* Timings (lab 20261104-203017, lane 2/3): death back in 5-8 s, editor 13-170 s (Tribe: the host's editor opened
  61 s after the vote once), menu 16-21 s, restart 17 s, rejoin 14 s.

## content_lookalike: a guest without two packs sees the host's creature (2026-11-04, fix/w43-content)
<a id="testing-content-lookalike"></a>

* `tools/lab.py run content_lookalike --lane 9 --skip 1,2,3,4,5,6,7,8` with `SPORE_FREE_LANE=10` (~2 min): host and
  Bob on the run's lane, Carol on the free lane with `bp1content/BoosterPack_01.package` and
  `Data/Spore_Pack_03.package` renamed `*.bug0559-off` for the test (put back in `finally`, and at the start of the
  next run if one died). The host's Tiger Horse gets Creepy & Cute parts, a Spore_Pack_03 foot, the base game's
  cell stripes paint and a pack detail paint. Checks on Carol's game: `coop.creations partlist` (roles of the
  model's parts, the species data's parts she lacks), `paints`, `parts`, 0 faults. FAIL with
  `SPORE_FIX_OFF=content.species_data` or `content.lookalike` (docs/re/creations.md#creations-lookalike).
* Looking at it: `CONTENT_HOLD=<s>` keeps the games after the checks (raise the suite's minutes for a long hold);
  `python3 tools/sporectl.py -i <inst> shot <path>` (a path inside the worktree: the game cannot write to /tmp),
  `coop.cam <x> <y>` gives the screen ray, `coop.abilities goto <x> <y> <z>` puts a game's creature there.
  `CONTENT_SWAPS=<old>=<new>,...` puts any parts into the host's model instead (the control run with base parts).
* Pitfall: the host's family members look like the host's creature: find a puppet by moving it (`goto`) between
  two screenshots. ? Seen once (not analysed): the partner names sat over a creature 10-15 m behind the creatures
  that moved with `goto`.
* Pitfall: `lab.py reload` of a guest that is in a session drops the packages co-op registered for it (the
  received creations: `partlist` answers `none` afterwards); inspect before a reload.
## Tests that waited for the wrong thing: the controller's bake, the cold join pause, meals, food on screen (2026-11-04, fix/w43-tests)
<a id="testing-harness-g1"></a>

G1 of 0.7.7 (BUG-0430 class: tests and harness that hide failures). Each change names the run where the old test was
wrong and the run where the new one is right; no threshold was loosened.

* **tribe_guest_tribes** (BUG-0587): (1) since W2 (realign.villages) its twin run `SPORE_FIX_OFF=tribe.guest_tribes`
  passed step 1 although the guest's controller formed three tribes: W2 removed them within 4 s (wrong run lab
  20261104-231126). realign.villages is now off until step 3 (right run 20261104-231338: step 1 and 3 FAIL as
  BUG-0483's FAIL). (2) Its step 2 ("tribes 2 -> 2", 12 of 13 runs of fix/w43-world-state, lab 20261104-212104 ..
  -214344, and fix/w43-realign 20261104-214542): the controller (0xCC6440) forms nothing while the next species'
  model is not baked (it asks for the bake and returns), without a spawn marker (noun 0x36BE27E, +0x108 ==
  0x57F09E5E; 4 on the fixture) or when the tribe nouns reach its property list. `coop.tribe form` now ends with
  `(next species <p> baked|not baked yet) (tribe nouns N, tribe spawn markers M)` and `(blocked: tribe.guest_tribes)`;
  the test calls it again while it only baked (at most 40 s). Which condition stopped it on lanes 4/5 is not
  confirmed (lane 2 always `baked`, 2 nouns, 4 markers). Disproved: the cold join's pause bit does not stop the
  forming (held by hand, lab 20261104-225835: `tribes 2 -> 3`).
* **play_tribe** (BUG-0586): "guest: the minimap takes the view where it points" failed when the calibration ran
  inside the guest's cold join pause (BUG-0444; `busy=1` at the failure in lab 20261104-220200 and fix/w43-realign
  20261104-213145): its minimap clicks did not move the view. The test waits for both games' clock to run
  (coop_sync.wait_running) first. Holding only the pause bit does not stop the view, so the bit is a marker of the
  loading game, not the cause.
* **cell_coop** (BUG-0356): its warm-rerun failures were the revive / respawn counters of the previous run (fixed by
  BUG-0396, ba7bb495, 40 of 40 runs ok since 2026-11-03); the cold one (lab fix/w31-civ-gate 20261101-100204,
  "revives 1, hp 6") was a meal right after the revive (own_eats 1 -> 2; AvatarEat adds the food's hpValue up to 6).
  The revive check allows hp 6 only when the guest ate since the knockout (`meals since the knockout` in the line).
* **cell_new_game** (BUG-0293): a probe try that found no meat on screen at once was spent after 60 frames ("none on
  screen" in 4 of 10 runs of 2026-10-30). A try now waits up to 20 s for food of its kind on the screen; a try with
  none in that time still counts as not eaten, and the 3-of-6 rule is unchanged. The first run after it still
  failed (lab 20261104-232622: the host found no meat in 20 s in 4 tries; the guest swam 6 times into one meat its
  game never ate, BUG-0589): the probe's failures are not only an empty screen.
* **The proxies gate** (BUG-0503, BUG-0440; play_tribe, long_tribe, chaos_tribe): the gate was right. A guest at 3-5
  fps under lab load placed a walking animal's proxy with walk_place's catch-up capped at 0.1 s frames and fell
  behind the host's animal (placed position 0.1 -> 7.3 m in 6 s, coop.npctrack on both games). Fixed in the mod
  (`npc.proxy_slow_frames`, max_dt 0.4 as the Tribe mirrors). `coop_npc_walk_slow.py` (suite `npc_walk_slow`, tribe
  fixture, ~1.5 min): the guest at 4 fps (`frame.limit 250`), the host's game walks 4 animals at 4-5 m/s for 40 s;
  per walker >= 40 walked rows, never 3 m off for 2 s (the monitor's rule), placed p90 <= 1 m. FAIL with the fix off
  (20261104-232034: placed p90 1.42-2.41 m), PASS (20261104-232210 x2: 0.00 m).
* Pitfall: at 4 fps the off rule alone (3 m for 2 s) passed the twin run (1.5 s at most): the placed-position check
  is what catches the fall-behind; look at the FAIL run's rows, not only its verdict.


## The live Tribe -> Civ change and pause reasons (2026-11-04, fix/w43-transitions)
<a id="testing-tribe-civ"></a>

* `tribe_civ` (coop_tribe_civ.py, tribe fixture, ~3.5 min): the first lab test of the live Tribe -> Civ change (the
  campaign jumps to the civ_npc fixture instead). `coop.tribe advance` fills the goal bar (cPlayer +0x10F0 = +0x10F4)
  and posts the stage's goal message 0x5CC2589: the HUD's advance button (window 0x6454B47, `coop.ui where
  6454B47`) appears; from there everything is the players' way: the button, the stage card's check (1415, 1055),
  the captions' check (1410, 1048) while `busy` shows cinematic + paused, the guest's "I'm ready", the City Hall
  and vehicle editors (3 parts dragged from the palette's first row, done, the name panel's dice (614, 826), done
  twice). BUG-0576 (SPORE_FIX_OFF=core.editor_own_pause: the editors 60 s after the votes) and BUG-0585
  (SPORE_FIX_OFF=tribe.food_fx_mat: the host freezes when the guest adds food in the vehicle editor).
  Runs: FAIL 20261104-224338 / 20261104-224915, PASS 20261104-224318 / 20261104-224930.
* A frozen game shows as a console timeout: the test prints a FAIL line and RESULT: FAIL for it.
* `coop.pauses` lists the pause reasons the game counts now (docs/re/tribe.md#tribe-game-time-pauses): a guest that
  just loaded a Tribe world shows `04BF38A7=1` for 7-9 s on GOG 2024 (the stage entry waiting for its bakes,
  BUG-0313); the end-of-Tribe stage card `04BF38A4`, an editor coming `04BF38A6`.
* A hot reload (`lab.py reload`) of a lane whose games are in a session left the host `offline` (seen once on lane 4):
  start the games again for tests that need the session.

## The game's audio commands in tests (2026-11-05, fix/w44-tribe-guest)
<a id="testing-audio"></a>

The lab games run muted, but the game still submits every sound to its audio system. `coop.audio mark` empties the
list; `coop.audio list [n]` prints the commands since (play 0x3475365, stop 0x347536B, ...; the listener's moves are only
counted) with the sound id, the track, the first parameters and `from=` the first four return addresses on the stack,
then `playing track= sound= since=` (played and not stopped). tribe_theme_music counts the plays whose second return
address is TribeTheme_Play's (0xBC2414). docs/re/tribe.md#tribe-guest-a1 lists the sound ids seen in Tribe.

## Detector reads that were no desync (2026-11-05, fix/w44-world-state2)
<a id="testing-detector-reads"></a>

* `SPORE_DETECTOR_DIAG="<cmd>;<cmd>"`: with any `:state:` or `:planet_owner:` finding the monitor prints both
  games' answers to these console commands as `DETECTOR-DIAG <side> <cmd>: <line>` (e.g.
  `coop.wsync;coop.wsync dump tribe_rel;coop.wsync want tribe_rel`); not counted by detector_report.py.
* A total a guest has not synced yet prints `money=100000(unsynced)`: read as its number (BUG-0595).
* Skipped as `apart` (not counted as findings): a game that lists no object while the other lists its world (it
  loads: a rejoin, BUG-0596), and the host's game in the end of Tribe (`st phase stage_end=1`: from the City Hall
  editor on it takes its village and animals out of the world for Civ, BUG-0588 / BUG-0594;
  `SPORE_DETECTOR_STAGE_END=1` compares anyway).
* Creature: a guest's species table is read twice, 1.5 s apart (`SPORE_DETECTOR_BRACKET=0`: once); a record in one
  game only must be so in both reads (the host's table reaches a guest within ~1 s, BUG-0596).
* The Tribe state rows name both games' political ids (`pol <host> / <guest>`).
## Tests that failed for the bot, not the game: NPC picks, the order pace (2026-11-05, fix/w44-tests)
<a id="testing-harness-g2"></a>

G2 of 0.7.8 (BUG-0430 class). For each: which kind of fault it was and the evidence; no check was loosened.

* **A test that takes "the first NPC of `coop.npc list`" takes whatever the mod numbered first.** Cold games list the
  same animals (#17-#21), warm games go on numbering (#40-#63) and which NPC comes first depends on the test before.
  team (BUG-0437) hunted #40 of the host's own species after puppet_fight (lab 20261101-171437): the joint hunt made
  the host bite its own family and the family bit back (the posse_damage gate). A test that needs a wild animal
  filters the host's species and the `family` mark (coop_team.py, coop_creature_air.py). Not checked yet for this:
  other tests that take the first listed NPC (e.g. coop_npc_run_hp.py, coop_team_real_damage.py).
* **creature_air** (BUG-0601): an animal that bites the guest on the ground often does not stay in reach once the
  guest is in the air (the game allows no melee at an airborne target 1 m or more away): it stood 12-14 m off or
  walked 30-120 m away in every species of the creature fixture, and the precondition "the NPC struck at the airborne
  guest" failed (warm after nest_heal in every run kept: rel/0.7.8 20261105-121717, fix/w44-guest-player2
  20261105-113638, -114336, -121010, here 20261105-124455, -124913; also cold here with another NPC first,
  20261105-130042). The guest is put beside its NPC every 5 s of a round when it is 3-15 m off,
  and an NPC farther off or without a strike in 3 rounds gives way to the next (up to 8). The twin
  `SPORE_FIX_OFF=creature.air_immune` still fails (3 bites in the air, lab 20261105-131430). nest_heal leaves nothing
  behind that the warm reset keeps: it only changes the numbering.
* **play_tribe "gave orders through real clicks"** (BUG-0397): the bot's own rate, not the game (12 of 37 kept runs had
  8-9 orders for one player in 10 min; the host's bot too, 0-9 in 5 runs). The random mix of camera moves, partner
  clicks, waits and gifts, heals with no food mat on screen and gathers with no tree in the open (that one was not
  even counted) left too few orders. Each bot now keeps a pace of one order per 30 s of play (`ORDER_EVERY`): behind
  it, it plays an order kind (the plan's, else a gather; a hunt when the last try found nothing). The floor (10) and
  the checks on the orders (acted on, lost, rejected) are unchanged; the summary line says how many steps the pace
  chose (`kinds chosen by the order pace`).
* **play_tribe "the members act on the orders"** (BUG-0505 class): the one failure of 0.7.8 (fix/w44-tribe-guest
  20261105-115848, 13 ok / 11 idle) was the game: members standing in the gather node with their walk stopped short,
  new orders not taking them out (BUG-0531, still open). This check measures the game; it stays.
* **cell_new_game's meat probe** (BUG-0589): meat is kill loot. A run where neither player found meat on its screen in
  any of 12 tries of 20 s (lab 20261105-132739 #2) measured the pool, not eating: the probe now bites the nearest
  creature on its screen (the game's bite, `coop.pool kill <key>`) when no meat floated by in half of its wait, then
  waits for the meat. Near food (under 8 m) is swum to from where the cell is, mouth first: the teleport of
  `swimto <key> tp` puts the cell beside the food without turning it (a guest sat 0.33 m from a meat's centre and did
  not eat it, 20261105-132739 #1). A try that ate nothing logs what the cell is in the eater's game (`coop.pool cell`:
  layer, level, size) and the avatar's size, hp and place.
* **tribe_guest_tribes** (BUG-0587): closed. No failure since G1's controller retry (0.7.7): 8 runs, 3 of them here on
  a loaded lane (lab 20261105-134354).
* **long_session transitions** (BUG-0430, long_creature_short rel/0.7.8 20261105-123757): a console call into a guest
  that loads the shared world can time out (a 52 s load); restart, rejoin, menu and wait_together take a timeout as
  "not yet" (`busy_ok`) and wait on until their deadline. A guest that never comes back still fails.
* The lab orders the tests of one call by their suite minutes (longest first, `lab.py` parallel sort; equal minutes
  keep the given order): with 6 min creature_air ran before nest_heal in `nest_heal,creature_air`. Separate lab calls
  start the games cold, so a warm pair must be one call with the first test not shorter than the second.
* **The proxies gate** (BUG-0440 / BUG-0445; chaos_tribe, play_tribe): its trace line `proxy N ... m off the host's
  NPC` now ends with the error when it went off, how far the proxy moved since and the host NPC's largest step between
  two audits (0.25 s) in the last 3 s. A host snap shows as a large step (several m) just before; the lines of G2
  (lab 20261105-135000 play_tribe#2) had none (0.6-1.5 m per 0.25 s at 2.5-5.5 m/s): proxies falling behind a moving
  host NPC, a real desync. The gate and its window stay.

## The guest's copy against the host's body at the same moment (2026-11-06, fix/w45-remote-bodies)
<a id="testing-body-trail"></a>

* `coop.bodyrec start|stop|count|dump [from] [n]` (mods/coop/coop.cpp, tests only, reads the game): every frame,
  every synchronized body of `coop.sync` (p players, n NPCs, t Tribe members, v Civ vehicles, s ships, c cells) as
  drawn (read at the start of the mod's frame, before messages and driving) with the absolute time
  (`H->time_seconds()` = QueryPerformanceCounter: one clock for every game on one PC, so the host's and a guest's
  rows line up without a clock offset). Ring of 400 000 rows.
* `tools/tests/body_trail.py`: `analyse(host_rows, guest_rows)` interpolates the host's body at every guest row's time
  and compares at the guest's distance from the planet's centre (the height is V1's rule, not measured here). Per
  kind over the samples where the host's body moves (> 0.5 m/s): distance p50 / p90 / max, lag (distance over the
  host's speed, s), the longest stretch over 2 m.
* `tools/tests/coop_body_trail.py --stage creature|tribe|civ` (suite `body_trail_creature`, `_tribe`, `_civ`,
  `_player`, `_tribe_player`, `_civ_loss`): the host walks NPCs / orders members / drives vehicles for 60 s and the
  test checks the p90 distance and the longest stretch (BUG-0425; `SPORE_FIX_OFF=drive.lead` fails Creature and Tribe).
* Measuring against the host's state that the guest received (npctrack's `tgt`, coop_sync's lag window) hides any
  lag of the target itself: a copy that follows a late target passes those checks.

## The detector's trail rows and the body_trail additions (2026-11-06, fix/w45-remote-bodies-2)
<a id="testing-detector-trail"></a>

* BUG-0609: with `SPORE_DETECTOR=<s>` the monitor also starts `coop.bodyrec on` (records without clearing a test's own
  rows) on the host and every guest and, every period, reads the rows since the last one (`coop.bodyrec since <t> [n]
  [step]`: one frame per 50 ms) and runs `body_trail.detect_findings`: a `<stage>:trail:<kind>` row (npc, member,
  vehicle, player, ship) when the guest's copies of a kind are late (lag p50 > 0.14 s) and off (dist p90 > 0.5 m) at
  the same moment, or one copy stayed over 2 m off for 1.5 s. `SPORE_DETECTOR_TRAIL=0`: off. Measured per 10 s: without
  drive.lead lag p50 0.15-0.23 s in every window, with it 0.013-0.132 s (labs 20261106-041119 / -041542 / -042100); the p90 lag
  is spread by the detector's own census dumps (both games hitch), so the median is used.
* `body_trail.steps(rows)`: one-frame steps over 5 m of a member in one game's own rows; body_trail_tribe fails on any
  (BUG-0610: the arrival's chief placing teleported a walking chief; it was in 11 of M1's 13 tribe tracks).
* `coop_body_trail.py --hits` (suite `body_trail_hits`, BUG-0409): the host hits a walking NPC every 5 s (every other
  one with a push of 3), the 3 s after each hit are judged on both games' bodyrec rows (max 3 m, over 2 m at most
  0.5 s); the npctrack rows of both games (with the locomotion mode `lm` = +0x2B0 and grounded `gr` columns) go to
  out/laneN/track/body_trail_hits_npctrack_*.json.
* `coop.track` rows end with the creature's +0xF90 (drawn flag) and +0xEAC (LOD) (BUG-0434).
* `coop_net_nan.py` (suite `net_nan`, BUG-0614): a scripted guest (fakepeer protocol) sends NaN / Inf states; its
  puppet must stay finite in both games' bodyrec rows.
* `coop_mound_view.py` (suite `puppet_mound_view`, BUG-0434): the host on the fixture's nest mound, the guest 10 m away
  turning its camera with right-drags for 60 s (~200 entries of the puppet into its view); fails on any frame on the
  screen that draws the puppet on the plain terrain (hd < 0.1 m while ht > 0.5 m).
* `coop_puppet_walk.py` now records both games' `coop.bodyrec` too: the puppet against the walker's own creature at
  the same moment (`body_trail.analyse` on `p` keys) and `body_trail.stop_slides` (how far past the walker's stop the
  puppet went). Suite `puppet_stops` (BUG-0408): `--dir both --guest-fps 30 --goto=-195,-465,-45 --stop-walk` over
  SPORE_NET=player (its `stops` check: slid on over 0.15 m after at most 10 % of the stops).
* The partner's puppet has no lead (M1, on purpose): its copy is 0.16-0.17 s (lag p50) behind the walker on a clean
  lab, p90 0.8-0.9 m at walking speed (puppet_stops bodyrec lines, lab 20261106-053105).
## NPCs while the host's game stands still, a guest's leave and rejoin (2026-11-06, fix/w45-one-game-objects)
<a id="testing-npc-gaps"></a>

* `coop_npc_frozen.py --case death` (`npc_frozen_death`, creature fixture, ~1 min warm): the guest goes to a nest
  150-260 m from the host (`coop.census all 3000`, noun 0x52AA6122, then `coop.npc gopos`: a point in the sea brings
  the sea monster, which killed the guest and respawned it next to the host in a first try), the host dies and keeps
  its death caption open (`frozen_s` of `coop.npc` > 2 s, ~8 s after the hit), the guest comes back next to the host:
  within 8 s every living host NPC within 50 m of the guest must be a proxy in its game (census of both).
  `SPORE_FIX_OFF=npc.frozen_send` fails (out/lab/20261106-014526).
* `coop_npc_frozen.py --case caption` (`npc_frozen_caption`): `coop.dna add 3000`, the brain upgrade caption up for
  14 s, the guest keeps the host's NPCs near it. A regression check (it passes without the fix: the loss of BUG-0413's
  transition_net run did not happen on the fixture).
* `join_during_host_editor` now checks the guest's planet while the host edits: its own creatures (census `local`,
  alive, AI on) within 70 m, at least 60 % of the proxies it showed before its leave; `SPORE_FIX_OFF=npc.leave_locals`
  fails (1 of 11, out/lab/20261106-015905). Their movement is printed, not checked (they idle at their nests).

## World objects at the host's places: coop_world_host.py (2026-11-06, fix/w45-world-objects)
<a id="testing-host-places"></a>

`tools/tests/coop_world_host.py --case <c>` (suites `host_fruit`, `host_bundle`, `host_bundle_civ`, `host_cultural`,
`host_space_ornament`) asserts the game's own object lists on both machines (positions within 1-1.5 m, or the same
count in a heap) for the kinds of mods/coop/worldobj.cpp that move or make the host's objects in a guest's game
(docs/re/creature.md#creature-host-places).

* `fruit` (creature, ~65 s): a guest's copy eaten locally only (`coop.wobj eat <p> local`, as the flora's recycling)
  and a guest's fruit moved 8.5 m (`coop.wobj move`); then every tree near the players must have the same fruit mask
  on both games (the made fruit took none). `SPORE_FIX_OFF=wobj.fruit_place` fails.
* `bundle` (tribe, ~50 s): `coop.wobj bundle ground x y z amount`, `bundle carry <member> amount` (the farthest member
  within 60 m: a near one put the food on the mat within a second), `bundle take <bundle>`. The carried bundle is
  tracked by its pointer (it moves with the member at ~7 m/s; the guest's copy is up to 3.5 m behind).
* `bundle_civ` (civ_npc): the host captures an NPC city (`coop.civ capture`), then the ground bundles of the whole
  planet are compared.
* `cultural` (civ_npc, ~2 min): the targets matched after a fresh load; the guest leaves and joins again after 60 s
  (they were 3-8 m apart after the rejoin without the fix).
* `ornament` (space, ~2.5 min): both land on the home planet (`coop.space land`), compare, `coop.space takeoff`, three
  times: the set of decorated cities differs per load in most landings. Only the 56 nearest the guest's ship are
  compared when the host has more (one snapshot).
## Placement and scale tests of fix/w45-placement (2026-11-06)
<a id="testing-placement-w45"></a>

* `civ_city_layout` (coop_civ_city_layout.py, civ_npc, ~2 min): the host founds 3 cities, captures one, moves one
  building off the guest's slot (`coop.civ bmove`, the state campaign_space showed), builds more; compares the game's
  building lists (`coop.civ blist <i>`) and the guest's buildings_fixed. SPORE_FIX_OFF=civ.city_layout fails (BUG-0602).
* `civ_vehicle_scale` / `node_scale` (~1 min each): one game's object gets the scale 10 the game gives it
  (`coop.civ vscale`, `coop.fields scale`), the other game's copy is read back. SPORE_FIX_OFF=civ.vehicle_scale /
  wobj.node_scale fail (BUG-0549, BUG-0485).
* `civ_city_ground` (~2 min) and `tribe_civ_ground` (coop_tribe_civ.py --ground, ~4 min): `coop.ground` on a 10 m grid
  round every city on both games (the BUG-0332 class in Civ; 0.00 m in every run so far).
* `tribe_member_place` (quiet tribe fixture) and `coop_tribe_member_place.py --attach` (SPORE_LANE=N, started ~75 s
  after a play_tribe of that lane): every 5 s the detector's compare of citizens, and for a lone host citizen the mod's
  nearest member, the guest's mirror, the object's pointer, vtable, age and dead flag (BUG-0620). The extra console
  load may matter: play_tribe failed once on the NPC proxies monitor rule (BUG-0503) with the watcher attached.
* `tribe_dead_member` / `tribe_dead_tribe` (coop_tribe_dead_member.py [--all]): an NPC member, or every member of an
  NPC tribe, killed on the host; the citizens at that place on both games (no host-only body: lab 20261106-072628,
  20261106-072755).
## The two games' ground: maps, stamps, a host reload (2026-11-06, fix/w45-village-ground)
<a id="testing-ground-maps"></a>

BUG-0332 (docs/re/tribe.md#tribe-terrain-stamps).
* `tribe_village_cadian` (coop_tribe_village_ground.py `--fixture=creature_cadian --map --terrain`, ~8 min): the
  players' way from the campaign planet's Creature to Tribe, then `coop.ground` on both games at 1499 points within
  45 degrees of the village (`--map`, also before the change) and on rings to 100 m round every village and tribe
  spawn marker (`coop.tribe markers`); `--terrain` compares the two games' stamp lists (`coop.terrain list`) and the
  ground at each stamp. PASS: everything within 0.3 m. Other flags (RE): `--watch` (the host's terrain 0-14 s into
  Tribe), `--host-reload` (the host loads its own save and is measured again), `--reapply`, `--dirty`, `--regen`,
  `--npc-at=x,y,z;...` (`coop.tribe npc_tribe_at`: SpawnNpcTribe at a point). The test waits for the host's
  tribe.host_reload load (`host_reloads=` in `coop.terrain`) before it measures.
* The `creature` fixture's planet shows the difference at most 0.2 m; the campaign's planet (creature_cadian) shows
  it in every run (0.6-14 m) but at other places: the tribe spawn markers are placed at random.
* `tribe_form_ground` (coop_tribe_form_ground.py, tribe fixture, ~1 min): the host's controller forms a village
  (`coop.res add food 250` first: tribe.form_pace holds it otherwise), the ground round it on both games.
* campaign_ct: `CAMPAIGN_GROUND=1` now probes every member, every village, the spawn markers, the six axis points and
  the Creature probe points again in Tribe (one line per stage, or when a guest differs by more than 0.5 m);
  `CAMPAIGN_FORM=1` makes the host form two villages right after the tribe setup.

## The last 0.7.9 tickets: attack reach, nest mound, bundle groups (2026-11-06, fix/w45-last)
<a id="testing-w45-last"></a>

* `coop.abilities reach` (debug.cpp): the avatar's combat target (+0xE7C) and its gap (`Ability_TargetGap`), every
  ability's type, row, reach and status (`AbilityBar_Status`), and the HUD bar's slots per row (the keys 1..5 use the
  stance's row). play_creature's bot (BUG-0618) clicks the target once and presses a key of the combat row: out of reach
  once (the game walks the creature there and uses it), in reach when the status is 0. `--old-attack`: the old loop (a
  key 0.2-0.5 s after every click). The log ends with each player's key presses by the game's reach
  (`press_in_reach`, `press_out_of_reach`, `retarget_*`, `wait_recharge`). The fixture creature has one combat key (1).
* `coop_npc_nest_backoff.py` (suites npc_nest_backoff, npc_nest_backoff_near; ~1.5 min warm): the host's player 120 m
  from the fixture's nest (-126 -488 -65), the guest at it, its camera turned (right-drags) onto the nest animal that is
  deepest in the host's surface; 10 s, the guest's roar, 14 s; `coop.ground x y z` gives each game's height over its
  surface (with the mound). Only samples on the guest's screen count (a game draws off-screen creatures on the plain
  terrain). BUG-0562.
* worlddiff (BUG-0617): food bundles are compared in three groups by the census's `own=` (ground, carried, held):
  `game_bundle` (on the ground, 3 m), `game_bundle_carried` (worlddiff.TOL_CARRIED 6 m: carried food moves with its
  member), `game_bundle_held` (another container, a food mat's). The census's `owner=` of a carried bundle is the
  carrier's player (tribe::owner_of; -1 for an NPC tribe's member). Selftest in tools_selftest.py.
* `coop.tribe who <citizen pointer>` (BUG-0620): its tribe (+0x1014), whether that tribe and any tribe list it, the
  mod's owner and id, age, dead flag, species, and the ai line; `coop_tribe_member_place.py --attach` prints it for
  every lone host citizen. `coop.tribe ai` ends with the order target's object (listed, noun, alive, place, distance);
  play_tribe prints every order nobody moved on with the host's ai lines (BUG-0531).
* New suites of fix/w45-last: `tribe_dead_player` (coop_tribe_dead_member.py --player, ~1.5 min warm: a host member
  killed with `coop.tribe pincer`, its body out of the member list needs a guest citizen within 3 m; BUG-0620),
  `host_bundle_carry` (coop_world_host.py --case bundle_carry, ~1.5 min: the carrier must walk (`coop.tribe order <id>
  4`), standing carriers bind both ways; BUG-0617), `tribe_gather_block` (a probe, BUG-0531: no stall seen),
  `npc_nest_backoff` / `npc_nest_backoff_near` (BUG-0562). play_creature's bot: the pack hunts together (no own hunt
  while the partner leads one).

## The shared headless Xwayland saturates under a full lab load (2026-11-06, fix/w46-tribe-fps)
<a id="testing-xwayland-saturation"></a>

Every lane's games draw through the one Xwayland of the headless shell (`:2`). With 10-14 games running (a 4-lane
final check with 4-game campaign tests, plus other lanes) that Xwayland used 97.7 % of a core (one thread) and the
games of the heaviest stage starved: in Tribe both games of a lane fell to 2-9 fps with 130-200 hitches of 300-600 ms
per minute while each game used only 5-7 % CPU (waiting, not computing). Cell and Creature games of the same run kept
25-66 fps. A 40-min test then fails with "the console stopped answering" or "FROZE" without a crash (BUG-0663).
Evidence: v0.7.8 and 0.7.9 run side by side (`long_tribe_short`, lab 20261106-235016 in rel/0.7.8 and
fix/w46-tribe-fps) collapsed identically at the same moment. Before reading such a failure as a regression: check
the Xwayland's CPU (`pgrep -x Xwayland`, its `/proc/<pid>/stat` ticks over 10 s; ~1000 ticks = one full core) and
run the A/B at equal load, or rerun on a quiet machine (1-2 lanes).

## A guest that joins while a member's body lies: tribe_dead_rejoin (2026-11-07, fix/w46-tribe-missing)
<a id="testing-tribe-dead-rejoin"></a>

`coop_tribe_dead_member.py --rejoin` (suite `tribe_dead_rejoin`, ~2 min cold, ~1.5 min warm): the guest leaves and
joins again; once it has left its world (the host saved for it) the host kills one of its own members
(`coop.tribe pincer ... 1000`), so the guest loads a world where that member is a body out of its tribe's list on the
host. For 24 s the host's `coop.tribe` bodies_sent tells whether the body is still sent, and the host's sync-check is
asked (`coop.diag sync now`) at each such sample. PASS: no MISMATCH. It is the short repro of long_tribe's "t: 1
missing on the guest" after its restart and rejoin events (45 min -> 2 min). With `SPORE_FIX_OFF=tribe.sync_bodies`
it fails 5-6 checks of 6 (BUG-0664, docs/re/tribe.md#tribe-sync-bodies).

## A body's mirror torn down with an active eat node: tribe_body_teardown (2026-11-07, fix/w46-tribe-fault)
<a id="testing-tribe-body-teardown"></a>

`coop_tribe_dead_member.py --eat` (suite `tribe_body_teardown`, ~1.5 min cold): on the guest `coop.tribe localbt <id> 1`
lets one mirror's behaviour tree run (as while the host is away), `coop.tribe eat <id>` orders its Eat at its tribe's
food mat in the guest's game (node 0x0461300F within ~1 s, read by `coop.tribe ai`), then `localbt <id> 0`; the host
kills that member of its own tribe (`coop.tribe pincer`), and the test waits until the guest's mirror of the body is
gone (~33 s: the host's game keeps the body, tribe.member_body) and checks the guest still runs frames 5 s later and
caught no fault. It is the short repro of long_tribe's guest fault at 0xAC9C7D and its "FROZE" (BUG-0667,
docs/re/tribe.md#tribe-body-teardown). `SPORE_FIX_OFF=tribe.body_teardown`: the fault (FAIL); with
`SPORE_FIX_OFF=tribe.body_teardown,kit.frameunwind` (or a SporeKit before it) also "FROZE": SporeKit's frame flag stayed
set after the game's handler unwound the frame, so frames stopped while the game went on.
* Pitfall: after a caught fault in a frame, a game's `frame` count standing still is not proof the game hangs: look at
  the watchdog's main-thread stack (App::Canvas::Tick pumping messages = the game runs) and for SporeKit's "the frame
  N was cut off by a fault" line.
## The Cell reports of 2026-11-06: kills and meals once, the level-change wipe, tokens (2026-11-06, fix/w46-cell-p0)
<a id="testing-w46-cell"></a>

* `coop_cell_once.py --case kill` (suite cell_kill_once, ~1.5 min cold): the guest bites a creature to death next to
  the host with the game's Bite and bites on for 1.5 s; the host's game must break each one once (`npc_deaths`).
  `SPORE_FIX_OFF=cell.kill_once` fails (3-4 breaks per kill). BUG-0632.
* `--case eat` (suite cell_eat_once, `SPORE_NET="delay=150"`): the guest eats a plant / meat cell (`coop.pool
  food_near`, `eat`) and repeats the eat every 40 ms for 1.5 s; it must find nothing to eat again.
  `SPORE_FIX_OFF=cell.eat_once` fails (1-3 eaten again per meal). BUG-0636. Without the delay the window is too
  short to show it.
* `coop_cell_levelup_view.py` (suite cell_levelup_view, ~2 min): the guest 30 m from the host, the food +-150 (one
  level is no wipe: RemoveCellsOfOldScale runs only when the world scale tier changes); each game's screen before
  and the cells of it still alive 5 s after (`coop.pool alive`). `SPORE_FIX_OFF=cell.levelup_keep` fails (the guest
  loses 100% of its screen). A level-up replaces ~90% of the screen in both games anyway (the game's own). BUG-0635.
* `coop_cell_push.py` (suite cell_push): each player swims into a creature of its size (`coop.pool gap near`,
  `gap <key>`): the guest is pushed as the host. BUG-0634.
* `coop_cell_own_level10.py` (suite cell_own_level10, ~3 min): the host's food to 924 (level 10), `coop.creations
  choose cell BAF1AD40` and back, `coop.pool species` (creature models with their levels). The game populates NPCs of
  the player's own cell (6-8 at level 10): the test checks no NPC of the previous own cell is left. BUG-0637.
* `coop_cell_once.py --case token` (suite cell_token_probe, extended) and `--case drop` (no suite: the fixture's
  kills drop no part token, so it always fails on its setup): BUG-0633.
## The address space census: what fills the game's 2 GB (2026-11-06, fix/w46-addr-space)
<a id="testing-mem-census"></a>

BUG-0631 / BUG-0621. The per-minute `perf:` line says how much address space is free; the census says **what** took it.
* `coop.memcensus [depth] [grain] [top]` (any thread, read-only): a VirtualQuery walk grouped by allocation
  (AllocationBase): `census free_mb= block_mb= regions= allocs= tracked=`, then `cat <kind> n= res_kb= com_kb=` per kind
  (`image:sporeapp.exe | image:coop | image:sporekit.dll | image:other`, `mapped:lt1M | ge1M | ge16M`,
  `priv:by:<module>` for private allocations whose maker is known, `priv:untracked:<size>` for the others) and
  `who <module+rva><<module+rva>... n= res_kb= com_kb=` (the callers, deepest first, of the tracked private allocations).
* `coop.memcensus track on|off`: a hook on ntdll's NtAllocateVirtualMemory / NtFreeVirtualMemory (test tool, never on
  by default) remembers every new reservation with up to 6 return addresses found on the stack outside ntdll,
  kernelbase, kernel32, the CRT, the game's allocator (GeneralAllocator: MallocInternal, MallocAlignedInternal,
  MMapMalloc and their helpers, its operator new) and the tracker itself. So `priv:by:sporeapp.exe` is the game's own
  heap (its allocator reserves 16 MB segments and MMapMalloc's big blocks with VirtualAlloc), `priv:by:d3d9.dll` the
  graphics, `priv:by:coop*.dll` / `sporekit.dll` ours (their CRT heap grows through the same call). Only reservations
  made while the tracker is on have a maker: compare two censuses of one game, not one census alone. The stack scan is
  a heuristic (a stale return address can show as a caller; the game's own frames are named only to the nearest
  `.sym` entry, which is often far away).
* `tools/tests/coop_mem_census.py` (suite `mem_census*`, extended): both games switch the tracker on, a scripted
  session runs, a census every `--sample` minutes, at the end the growth per kind and per caller between the first and
  the last census (`MEMCENSUS {json}` lines with the series). Sessions: the play_creature bots (default), `--solo`
  (the guest's game is closed first: `coop.leave` alone was not enough, the guest joined again), `--idle`, `--tour
  host|guest` (that avatar goes to a new place of the planet every `--hop` s with `coop.abilities goto`, the same
  sequence in every run), `--places N` (the tour cycles over N places: a cache fills in the first round, a leak grows
  every round), `--respawn` (`coop.puppet respawn` on both every `--hop` s), `--crowd N` (Cell: `coop.pool crowd N 8`
  every `--hop` s, over 256 cells in the active area: BUG-0355's case).
* Under Wine the lanes' games have 4 GB (`of 4095 MB` in the perf line) although the GOG 2024 exe has the
  large-address flag off (Windows: 2 GB): a lab game never runs out; judge the loss, not the free MB.
* Noise to expect in a census diff: `mapped:ge1M` -24 MB (mappings of the start released in the first minutes),
  `priv:untracked:ge16M` +-16 MB (one block of before the tracker freed or taken), the game heap growing in 16 MB
  segments (a step of 16 MB is one segment, not 16 MB of new objects).
* Gate findings of the census tests: the tours (`--tour`) put the partner 12-36 m off its own game's ground for 2
  monitor lines on some hops (`drawn` / `ground`, sync-check `p apart`): the teleports' own doing, BUG-0658; the tour
  entries run with the sync-check and monitor gates off. `mem_census_cell_crowd` with BUG-0355's fixes off had one
  sync-check `c: 11 missing on the guest` right after a `coop.pool crowd` moved ~300 cells at once (lab
  20261106-210201); none in the other off run or the two on runs. Its FAIL run (fix off) also loses 8.3 MB/min: that
  is the bug the test catches, and `mem_reserve`'s FAIL run (core.mem_reserve off) trips the mem-low gate on purpose
  (260 MB free, largest block 16 MB, planted with `coop.diag fill`).

## The Tribe and Civ reports of 2026-11-06: designs, level, portraits, colours, attacks (2026-11-07, fix/w46-tribe-civ)
<a id="testing-w46-tribe-civ"></a>

* `civ_host_design` (BUG-0647, ~1 min, cold): writes a vehicle model into each lane profile's My Creations under one test
  id before the start (another base game model for the guest: two players with a creation of that id), makes both civs
  name it (`coop.civ setdesign`), the host spawns a vehicle; the guest's mirror's model key (`coop.civ` veh lines
  `model=`, vehicle +0xC4) must answer the host's bem (`coop.creations modelhash`), also after the guest joins again (a
  bound vehicle of the loaded world). `SPORE_FIX_OFF=creations.host_designs` fails.
* `tribe_level` (BUG-0645): `coop.tribe level 2` on the host; the guest's `coop.tribe` player line `level= cap= hud_cap=`
  (cTribe_MaxPopulation and the HUD). `SPORE_FIX_OFF=wsync.tribe_level` fails.
* `tribe_portraits` (BUG-0643, ~1.5 min): the host's tribe becomes the host's own TigerHorse (`coop.tribe setspecies`),
  the guest's game is killed and started again (the game keeps the pictures it drew per process) and joins; the guest's
  game must answer the species' png (`coop.creations png`). The roster column's white pixels are printed only (the
  guest's sky is white for a moment after the load). `SPORE_FIX_OFF=creations.png` fails. `coop.creations fakepng <inst>`
  registers a broken picture (the failed-image square in the roster, when done before the first load).
* `tribe_colors` (BUG-0644): `coop.tribe npc_tribe`, then `coop.tribe color <i> <id>` on the guest; every village's
  `color=` must be the host's within 8 s. `SPORE_FIX_OFF=wsync.tribe_color` fails.
* `civ_guest_attack` (BUG-0648, ~2.5 min, regression only): the host gives two military vehicles to the guest, the guest
  attacks an NPC city with real clicks (city, crosshair, as play_civ); the host's city loses hit points and its capture
  meter rises. `coop.civ attack` now also works on a guest (the guest game's own SetOrder, sent to the host);
  `coop.civ tables` prints `last_order=` of the guest.
* `coop.audio` comparisons of an idle Tribe village (BUG-0646): footsteps (0x4A776923) are host-only (BUG-0669); a 30 s
  day (`call 0x00BC29A0 thiscall <cTimeOfDay::Get()> 0x41F00000`) did not bring the fire dance in 3 min.
## A guest's tribe mapping held over a reload (2026-11-07, fix/w46-tribe-load)
<a id="testing-tribe-world-map"></a>

BUG-0672, `coop_tribe_world_map.py` (suite tribe_world_map, ~70 s on a cold lane). A rare race (the first tool
snapshot of a new world before its first tribe state) is made certain with two guest test commands: `coop.tribe
statehold 1` (the host's tribe states are ignored: the mapping host tribe index -> local tribe stays as it was) and
`coop.tribe frametool <index> <type> [m]` (make_tool from the next frame's tick, as the wobj snapshot runs it, with a
task for outside the frame queued just before). Turn the hold off at the end (`statehold 0`): it is not reset by a
session or world change. Frames are checked with the `frame` command 5 s and 15 s later. With `kit.outpump` off a caught
fault inside a frame with an outside task pending freezes the mod (the game's message loop never returns): any test can
show that, this one does on purpose.
## coop.bodyrec diag: where a remote body's driving aimed (2026-11-07, fix/w46-k3-bodies)
<a id="testing-bodyrec-diag"></a>

`coop.bodyrec start diag` (coop_body_trail.py with SPORE_BODYREC_DIAG=1) also records on a guest, every frame, the
driving's own targets next to the drawn bodies: per Tribe member mirror `a<id>` (the aim), `r<id>` (M1's raw lead),
`l<id>` (lead lateness s + one frame, 1 = no lead, the walk error m); per Civ vehicle mirror `w<id>` (its target) and
`e<id>` (state age s, the estimate's speed, the error). body_trail.analyse skips these keys. With them a trail splits
into the target's error (aim vs the host's body at the same moment) and the walk's (drawn vs aim): BUG-0410, BUG-0438
were mostly the first. A Civ or Tribe trail test at a lab load over ~10 on 6 cores measures the games' frame rate
(16-20 fps: states every 0.14-0.39 s): judge such runs only A/B against the switch off at the same time.
`coop.tribe undo <id> <s>` (guest, tests): the mirror's walk and unmarked placings are undone for s seconds (BUG-0386).

## Lab log signals that are not bugs, and detector classes (2026-11-07, fix/w46-s1-sweep)
<a id="testing-log-signals"></a>

From the S1 lab log sweep (703 runs, 2026-11-05/06) and the runs since:
* `world: cinematic 8915F9BB still playing at the load: ended it` (about 1600 lines, every run that loads from the
  menu): the galaxy menu's own intro, ended in the menu before a load on purpose (world.cpp end_menu_intro, docs/re/space.md
  7.9). Now an INFO line "the galaxy menu intro (8915F9BB) ended at the load, in the menu" (BUG-0654). The same intro "in a
  stage" (about 1000 lines) stopped with world.intro_at_once (BUG-0626): 0 lines in the 0.7.10 runs. Any other
  cinematic ended by force stays a WARN (one: F6A4B1E1 before a held editor, BUG-0404's designed step).
* `net: the session with the host ended: connection to host lost (timeout)` / `no answer from host (timeout)` on a
  guest: in all 8 cases (2026-11-04..06) the host's game had crashed or stopped first (its log ends 29-30 s earlier:
  crash tests with a fix off, a dev branch's double hook, a host hang in rel/0.7.8): the guest's timeout is right
  (BUG-0655). Look at the host's log end before suspecting the guest.
* `game fixes: skipped the tribe camera set-up without a simulator camera`: 7 of 7 cases (2026-11-01..05) within 0.1 s
  of a Tribe load with the menu intro playing into the stage (BUG-0607 / BUG-0626 class); 0 in 1905 Tribe loads since
  BUG-0626's fix. The line now names the mode and the playing cinematic (BUG-0656).
* `frames: Present has not reached SporeKit for 2 s` right after a hitch over 2 s: fixed (kit.fix presentlost,
  docs/02-modkit.md#modkit-present-hitch, BUG-0653).
* Detector classes (BUG-0657; `persist` = the next sample of the run still has it): Civ projectiles and AoE areas
  (default_tool, resource, cultural, default_ao_e_area; persist 0-25 %, only in fights) are shots relayed in 0.2 s
  batches that live under 2 s (BUG-0477 / BUG-0530: by design); Civ `state:civ_only` (persist 0 of 11) and
  `state:player_cities` (0 of 4) come with a capture or a new city on the host and are gone in the next sample
  (transient); `Creature:state:species_killed` (0 of 7) is a kill the guest's game counts a moment later: now compared
  with the guest's second dump (worlddiff, as species_only); `Space:noun_only_host:city_walls / city_territory` (and
  the planet's cities, buildings, vehicles, herds) are a planet's surface loaded by a game whose camera is on it: not
  compared in the whole list in Space (worlddiff SPACE_PLANET_LOD); Space `default_beam_projectile` in the host's
  game only (persist 11 of 14) is real: Space shots are not relayed (BUG-0677, open; fixed unreleased, see
  [the Space shots test](#testing-space-ship-shots)).

## The guest's cargo hold across a rejoin (2026-11-07, fix/w46-space-items)
<a id="testing-space-kept-hold"></a>

* `coop_space_items_rejoin.py` (~1 min, `space` fixture, BUG-0678): makes the two holds differ (the guest: spice 0
  x7, an Antimatter Bomb `144DF50A` at 9 charges, the interstellar drive; the host: spice 1 x5, the bomb at 3), then
  the guest leaves and joins again; it reads the game's `cPlayerInventory` on both with `coop.space trade inv`
  (`item type=.. key=.. count=.. ammo=..`). `coop.space trade kept` shows the host's kept holds and the guest's
  restore. `SPORE_FIX_OFF=space.keep_items` fails: the rejoined guest has the host's spice and bomb charges and no drive.
* The `space` fixture's ship has 8 tools (none with charges) and 10 cargo slots: add a charge tool first
  (`coop.space tool add 0x144DF50A`).

## A console NPC is no saved stray (2026-11-07, fix/w46-misc)
<a id="testing-console-npc"></a>

BUG-0673. `coop.npc spawn <hex>` makes its creature in a herd of its own (game::create_herd, no archetype): no tuning
(+0xE84), the look of a saved partner's puppet. From 0.7.8 on `npc.stray_ids` (BUG-0385) gave such a creature no NPC
id, so nest_species, creature_desync and old_species never saw it in the host's `coop.npc list` (nest_species failed
4 of 4 on rel/0.7.10). The creatures of `coop.npc spawn` are exempt from both stray rules now (`tools.console_npc`;
off: nest_species FAILs, 20261107-113816; on: PASS 20261107-113618). Tests no longer need `coop.npc strays off` for them.
## The galaxy menu's rollover over a load, and fault storms (2026-11-07, fix/w46-rejoin-crash)
<a id="testing-menu-rollover"></a>

BUG-0676, `coop_menu_hover.py` (suite menu_hover, ~1 min on a cold lane; cold only). `twoinst.start(...,
before_join=fn)` calls fn(guest) right before the guest joins (cold start, so the guest's first galaxy menu can be set
up; the guest's menu makes its planets ~15 s after it came up: poll `coop.menuhover` for `rebuilds 0`). `coop.menuhover
[arm <i> | off]` prints the menu's planets (count, panels, room = capacity) and its rollover / picked planet; `arm i`
makes the next planet rebuild put the mouse on planet i first and log the rollover after it. The fault needs the
rebuild to reallocate (more planets than the menu ever had: a cold guest, Game0 6 -> the co-op slot 7) or to shrink
below the rollover's index; a warm rejoin (the same slot again) does not fault. `coop.menuslot <GameN>` (outside the
frame) shows another slot's planets in the menu. A caught fault in every frame is named by SporeKit as `fault storm:`
(an ERR line, one report per run): grep for it in any test that ends with a guest stuck in a menu.
## The detector compares Cell positions in one world scale (2026-11-07, fix/w46-detector-scale)
<a id="testing-detector-cell-scale"></a>

BUG-0662. A Cell level change rescales every position about the origin over ~3 s (WorldScaleStep, BUG-0511), on
each game at its own moment, so a comparison that fell into a change found 80-552 cells 7-14 m "apart"
(`Cell:pos:cell`) with nothing to see. `coop.census all` now ends the Cell `me` line with the game's world scale
(`me pos=(x y z) ok=1 wscale=<G+0x514C>`), and `worlddiff.diff` converts the guest's Cell positions (rows and `me`)
into the host's scale (times guest scale / host scale, as the sync-check's `diag.cell_scale`, BUG-0606) before it
compares, and the cells' sizes (`scale=` of the l rows) with them: the rescale 0xE6DB70 divides pos `+0x4C` and size
`+0x58` alike (docs/re/cell.md#cell-world-scale), so the same change also gave `Cell:scale:cell` rows (191 cells
0.91 on the host, 0.80 on the guest, lab 20261107-141059 with positions only converted). A real offset stays: a cell
10 m off, or of another size, in the host's scale is still a row. A dump without `wscale` (an older coop.dll) is
compared raw. `SPORE_DETECTOR_CELL_SCALE=0`: raw positions and sizes (the old behaviour, to see the test fail). Selftest: `tools_selftest.py worldstate` (BUG-0662 case;
`SELFTEST_WORLDDIFF=<old tools/tests>` fails it).

## A guest's own Tribe game over, and campaign_tc2 (2026-11-07, fix/w46-tribe-leader)
<a id="testing-guest-game-over"></a>

BUG-0685. `tribe_leader_ko` (3 min, tribe fixture, warm): the host knocks out every member but the chief, then the
guest's game hits its mirror of the chief (`coop.tribe hurt <id> <dmg>` on a guest: the citizen's TakeDamage in that
game, a test hook); the guest must show no "Alas" caption and keep its input lock at 0 for 20 s.
`SPORE_FIX_OFF=tribe.mirror_blow` fails it (the lock at once, the caption after 12-19 s). A blow of 1000 by
`coop.tribe pincer` on a chief that still has members with health stops at 0.3 health: knock the others out first.
`campaign_tc2` (about 40 min, lanes as a 4-player campaign): the campaign from the Tribe fixture to Civ, the 8 minutes
of play and the raids of the whole campaign's Tribe stage without the 60 minutes before it. The guests' mirror log
lines (`tribe: mirror #N goes: ... (BUG-0685)`) say when and why a member mirror went or was not made again. Not in
any group (run it by name). First run (lab 20261107-163249, lane 4, the log lines without tribe.mirror_blow): no
`t: missing`, its gate failed on known classes only (`t: 1 apart`, the BUG-0410/0674 class; an NPC proxy 3 m off).

## A Creature UFO visit without a ship model: creature_ufo_nomodel (2026-11-07, fix/w46-advance-reg)
<a id="testing-creature-ufo-nomodel"></a>

BUG-0686 (docs/re/space.md#space-ufo-gfx-null). `coop_creature_ufo_nomodel.py` (suite `creature_ufo_nomodel`, two
games, the creature fixture) copies the BUG-0546 cache over the host's Pollination.package as space_npc_model does
(`--cache lane` keeps the lane's own), starts the Drama Manager's UFO visit (`coop.drama start 2`) until one ship gfx
is made without a model (up to 6 visits; the first did on lane 5), then lets it go on 40 s: no host fault. SKIPs when
every visit's ship had a model. About 3 minutes. `SPORE_FIX_OFF=game.ufo_gfx` must FAIL (lane 5:
out/lab/20261107-185421: 0xE99524 on visit 1).

Lab note: lanes 2 and 3 have host caches of UFO summaries only (2026-11-07): a campaign whose Creature end brings a
UFO visit faulted there at 0xE99524 (campaign_tribe "stopped in creature"), lanes 4 and 5 do not. A campaign_tribe
A/B between builds must use the same lane.

## A host turret's shots on a guest (2026-11-07, fix/w46-issue198)
<a id="testing-civ-turret-shot"></a>

* `coop_civ_turret_shot.py` (suite `civ_turret_shot`, warm, fixture civ_npc, about 105 s on lane 2): the host adds
  two turrets to the NPC city nearest to its army (`coop.civ addturret`), waits until the guest has them (civ.turrets),
  probes where each turret holds its combatant (`mem.read` of the turret, the dword 0x1466C28 at +0x588), then its
  military vehicles attack that city (`coop.civ attack <id> <city> 2`) and the turrets fire at them for 60 s.
  PASS: the host's turrets fired (`turret_shots: seen`), the guest fired copies (`fired`, no `bad_combatant`), both
  games answer and have no fault. Twin: `SPORE_FIX_OFF=civ.turret_weapon` (an execute fault per copied shot on the
  guest, BUG-0688; FAIL lab 20261107-204522). The first civ_npc test with a turret that fires: the fixture's NPC
  cities have none near a fight, so add them with `addturret` when a test needs turret fire.

## Civ: a paid build the money does not cover (2026-11-07, fix/w46-v2-civ-space)
<a id="testing-civ-money-short"></a>

BUG-0613 (docs/re/civ.md#civ-spend-below-zero). `coop_civ_money_short.py` (suite `civ_money_short`, warm, the `civ`
fixture) sets the shared money to 500 (`coop.res add money`), then the host builds a factory (1200) and the guest a
house (1600) with `coop.civ build`: neither is built, each game's own money is the same and not below 0; with 10000
the host's factory is built on both and paid once. About 1 minute warm. `SPORE_FIX_OFF=civ.build_money` must FAIL
(host -660, guest 0). civ_coop gives the host 5000 before its factory: the fixture's 3000 is spent by then.

## Space: the sync-check and a fast ship that brakes and turns (2026-11-07, fix/w46-v2-civ-space)
<a id="testing-space-fast-sync"></a>

BUG-0547 (docs/re/space.md#space-copy-on-path). `coop_space_fast_sync.py` (suite `space_fast_sync`, cold, the `space`
fixture, ~5 minutes) sets `SPORE_NET` itself when the lab gives none (delay 40, jitter 20, a 300 ms spike every ~10 s
for 6 s): on a local lane no flight pattern made the sync-check's copy differ at all (W pulses, braking, zooms, a
disabled guest; ~600 checks), the copy only falls behind its speed allowance when its speed changes within the moment
(the rescue flight) or a state comes late. Checks: moments with the copy within 10 m of the host's path happened, the
sync-check took none of those for a difference, no MISMATCH, and `coop.diag sync force shift` is still one. A copy off
the path (lab 20261107-215251 #1: 11.4 m past a ship that had stopped, a latency spike's extrapolation, BUG-0674) stays
a first-look difference that its recheck clears. `SPORE_FIX_OFF=sync.own_path` must FAIL (every moment is a difference). The forced MISMATCH is inside
`LAB-MARK sync-force` lines (the lab's gate leaves it out).

## play_civ: the gift and claim checks look in time (2026-11-07, fix/w46-v2-civ-space)
<a id="testing-play-civ-checks"></a>

BUG-0325. play_civ's pending checks (`Game.expect`) are looked at only in the audits (every 8 s plus the audit's
time). The gift check therefore polls the giver right after its click (4 s, every 0.2 s) and compares what the giver
sees with the host's owner of that moment: the partner's bot often gave a vehicle back 1.4-7 s after the gift
(`give_returned`). The spice claim check gives the guest 4 s after it first sees the host's claim (the host sends
the node owners once a second, a guest in a city planner takes them on its return: `claim_seen_early`). A rejected
guest order now names its target in the host's log ("target kind K id I ... not in the host's game").
## A UI click waits for held frames: look again until the game ran (2026-11-07, fix/w46-p4-creature)
<a id="testing-chat-held-send"></a>

* A `click` (and `key`, `text`) goes into the game window's queue at once and returns; the game takes it on its next
  frame. While the main thread holds its frames (a hitch, a save, `kit.test.hang`) the click waits: a check of what
  it did, made a fixed time after it, can come before the game took it (BUG-0629: play_creature called a chat line
  lost that both games showed 0.4 s after the bot's single look, after a row of 0.4-1.1 s hitches).
* `frame` and `coop.status` are answered off the main thread, so they work during the hold: a check that fails
  looks again until both games have run a few frames since (`play_creature.chat_reached`: 5 frames, 8 s at most).
* `kit.test.hang <ms>` holds the main thread on its next frame (2.5 s passes the hitch gate in chat_pause and
  chat_held_send); `tools/lab.py run chat_held_send,chat_held_send_oldcheck` shows the late click (the _oldcheck twin
  with SPORE_CHAT_WAIT=0 must FAIL).

## README media (2026-11-07, docs/readme-2)
<a id="testing-readme-media"></a>

* `SPORE_LANE=3 python3 tools/tests/readme_shots.py [cell creature tribe civ space gif menu]` makes every
  picture of the README (docs/media/, 1280x960 JPEG under 195 KB) and `coop_creature.gif` (~7 MB); ~6 min for all.
* `shot <path>` takes ~45 ms at 1440x1080, so a frame sequence at 10 fps works (the GIF: 100 shots in 10 s).
* `coop.overlay off` hides the status block and the players' name tags too: the README pictures keep the overlay on.
* Two players walk side by side by real input: right after a load both cameras face the same way, so W held on
  both games at once moves them in parallel; the guest is first put 2-3 m to the side with `coop.npc gopos`
  along the host camera's first matrix row (`coop.cam` m[0]). Click-to-follow (the guest clicking the host's tag)
  left the follower behind the host's camera or turned it into a social target (green circle, no walk).
* `key esc` with nothing selected opens the game's pause menu; clicks meant for the ground then land in it.
* The guest plays its own creature with `coop.creations choose creature <instance>` (`coop.creations list creature`);
  since BUG-0638 a fresh load gives it the host's species until it chooses.
* In the galaxy menu, hosting with a guest joined, the lobby's hint line reads "Select your units to give them to a
  partner" (a hint for Tribe / Civilization shown in the menu).
## Tests of the not-reproduced crashes and freezes (2026-11-07, fix/w46-n1-crashes)
<a id="testing-n1-crashes"></a>

0.7.11 N1 (BUG-0026, 0118, 0167, 0170, 0171, 0172, 0184, 0578). Each a one-lane test unless named.
* `tribe_camera_null` (BUG-0167, cold, ~1 min a try): a guest joins 12.5 s after its galaxy menu came up
  (`twoinst.start(join_at=)`) with `world.intro_at_once` off before the join: the menu's own timer starts the intro in
  the first frames of its Tribe and the tribe setup meets the null simulator camera (4 of 4 on the first join).
  Since 2026-11-08 the guest is also armed with `coop.introstage` (the intro in the first tick of its next stage): the
  join time alone misses the race when the guest's console answers late (lanes 2 / 3, ~30 s).
  SPORE_FIX_OFF=tribe.camera_guard fails with the report's fault. Intros started by hand (PlayCinematic at the load
  request, in the Loading mode, on a host's reload) do not make the null camera: only the menu's own timer does.
* `creature_death_coop` / `_slow` (BUG-0026): `coop_creature_death.py --coop`: the four skip variants (Esc during the
  caption, clicks on the cutscene, Esc after OK, Enter spam) with the session kept, first the host dies, then the
  guest; `_slow` at 20 fps over the player network.
* `tribe_epic_dying` / `_slow` (BUG-0171): rounds of a new co-op epic, both members' blows until it is at 0, 20 more
  blows at once; both games must keep drawing. SPORE_FIX_OFF=tribe.hit_dead lets the blows at hp 0 through
  TakeDamage (the 2026-10-22 loop): no freeze either way on GOG 2024.
* `tribe_gameover_reload` / `_reload2` / `_slow` (BUG-0172): `coop_tribe_gameover.py --after-ok S --count N`: the
  member spawns come during the bounce back's reload, or 6 at once at 20 fps over the player network.
* `menu_escape` (BUG-0184, one game, ~6 min): six starts of the game, each with the menu's own intro (once per start,
  ~16 s after the menu came up), "Co-op" clicked 0-7 s into it or Escape at its start, then Escape with no intro; the
  frames must go on and the co-op window open. Not models of the player's intro: the intro started by hand
  (PlayCinematic) does not end on the mod's Escape, and a return to the menu (`coop.load`, `coop.menu`) does not start
  it again.
* `exit_editor_held` / `exit_editor_open` (BUG-0578, ~4 min): the game's own exit (`coop.close`: WM_CLOSE to the game
  window, cSporeApp::Shutdown, the editors disposed) with the City Hall editor held (core.editor_own_pause off, the
  0.7.3.6 state) or open; the test checks the process ends and writes no crash report. The lane's other game (same
  bottle) ends with it, so the test checks the guest before the exit.
* `multi4_tribe` / `multi4_tribe_slow` / `multi4_creature_seed` / `multi4_creature_slow` (BUG-0167, BUG-0170, ~7 min):
  `coop_multi.py` with 4 players in one lane (instances 20+N / 30+N), 6 min of chaos-bot churn; the creature ones
  replay seed 1035603402 of the BUG-0170 report; `_slow` at 20 fps over the players' networks.

## State classes: world filter, ids, effect height (2026-11-07, fix/w46-k2a-state)
<a id="testing-k2a-state"></a>

* `world_gate_vote` (coop_world_gate_vote.py, 6 min, cold): the guest loads a copy of the creature fixture planet
  under another name WHILE it stays in the session (`coop.worldfix autoload 0`, `coop.load Game9 Otherworld`), sets
  a species relation of its own (`coop.social record`), the host runs a vote (`coop.vote editor`, `coop.vote go`).
  Twins: `social.world_gate`, `core.vote_world` (BUG-0703, BUG-0702).
* `civ_siege_npc` (fixture civ_npc): the game's capture meter through `coop.civ siege <city> <player>`; player 254 is
  an NPC nation's vehicle (BUG-0704, `civ.siege_players`).
* `space_friendly` (fixture space): `coop.fleet spawn 2 own` is an escort of the host's empire; the games' hp show
  the game ignores damage within one empire with or without the filter (BUG-0705).
* `player_forget` (fixture space, cold): `coop.players held <id>` lists the per-player state the modules keep
  (player_life.h); nothing after a leave (BUG-0706, `core.player_forget`).
* `effect_ground` (fixture creature, warm): `coop.effects inject 0 0 0 5 <id> 0 x y z <agl>` feeds a world effect as
  the host would send it; the 11th argument is the height over the sender's ground (BUG-0608, `effects.ground`).
* `space_own_save` (fixture space, cold): the guest leaves, loads a copy of the fixture slot under another planet name
  (Otherspace) and joins again; `coop.res add money` on both sides (BUG-0709, `res.space_save`).
* `npc_max_hp` (fixture creature): four worlddiff comparisons, no `max_hp` row (BUG-0710, `npc.max_health`). The
  switch must be off from the start of a run's first states: drive_puppet's stored max stays until the entry is
  forgotten (the off side forgets it at the next state).
* Part B of K2a (fix/w46-k2a-state-b): `res_away` / `res_away_tribe` (`coop.res block <s>`: the host's resource reads
  as unavailable; `coop.res away <s> <res> <delta>`: the guest's tick sees an editor's mode, then buys), `civ_away_buy`
  (the host's real main menu and reload), `space_reward_away` / `space_contract_clock` (`coop.space away`,
  `coop.space contracts result`), `res_cause`, `space_partner_hp` (`coop.puppet hp`), `species_table`,
  `creature_stun_proxy`, `tribe_member_age` (BUG-0691..0700).

## Remote bodies under spikes: what the K4 tests read (2026-11-08, fix/w46-k4-bodies)
<a id="testing-lead-fall"></a>

BUG-0674, BUG-0675, BUG-0651, BUG-0680 (docs/re/tribe.md#tribe-lead-fall).
* `net_tests` test_lead_fall is the deterministic part: a member turning round / stopping / going straight on inside a
  0.35 s spike, a 28 m/s ship stopping in a 0.4 s spike, an NPC proxy's walk known late, a thrown NPC with two of three
  states lost, a member whose loaded sender stamps its states off its frames, the no-lead rule against a 140 ms mean
  with a 45 ms link, and a Creature puppet whose partner stops in a spike. Each CHECK of the "0.7.10" side fails with the
  fix on and the fix's side with it off.
* The guest's game log now has `net: round trip p10 .. p25 .. p50 .. p90 .. ms, mean .. ms (BUG-0651)` every 10 s:
  the samples the no-lead rule reads (p25 / 2 with drive.lead_low). The player network reads p25 80-152 ms at a lab
  load of 25-35, the mean 115-462 ms.
* The lab load decides much of body_trail_tribe_player: at a load of 30-35 on 6 cores both games run at 10-16 fps,
  the members' lag p50 rises to 0.10-0.21 s and the test fails with any switch set (lab 20261108-010745 #1 off,
  -010745-2 #1 on); compare fixes with an A/B on two lanes at the same time, never with runs of another hour.
* npc_walk_tribe at `SPORE_FPS=30 SPORE_NET=player` counts the proxies' jumps (> 2.5 m in a frame): a walk that starts
  in a spike is known 0.3-0.5 s late, the proxy's target jumped at the start (late_lead brings it in now).
## The monitor's drawn rule in Cell level changes (2026-11-08, fix/w46-k7-findings)
<a id="testing-monitor-cell-scale"></a>

BUG-0681. In Cell the own trace line ends with `wscale=<k>`, the game's world scale (cellsync::scale_now, G+0x514C)
its positions are in. During a level change each game rescales at its own moment (docs/re/cell.md#cell-world-scale),
so for a trace line or two the viewer draws the partner in one scale and the partner's own `pos=` is in another (raw
distances up to 20-32 m in cell_levelup_view). The drawn rule (tools/tests/monitor.py Drawn.in_scale) converts the
partner's position into the viewer's scale (pos x own scale / viewer scale) when both own lines of the moment carry a
scale; logs without it are compared raw. `SPORE_MONITOR='{"drawn_scale": 0}'`: raw (the rule before). Planted cases in
gates_selftest (`f_drawn_scale`, `_off`, `_real`, `_old`).

## Tests of the 0.7.10 final-check findings (2026-11-08, fix/w46-k7-findings)
<a id="testing-k7-findings"></a>

* `planner_leave` (BUG-0682, ~4 min, space fixture): the guest calls planner_city() every frame (`coop.civ planner_probe
  on`) and quits to the galaxy menu 4 times (`coop.menu`); `stale_reads` must stay 0 and no game may fault.
  SPORE_FIX_OFF=civ.planner_stale: 11-12 stale frames and 8 caught faults in game::cast per run.
* `civ_village_bundle` (BUG-0684, ~2 min, civ_npc): the guest's pointer is moved over an old village both games have
  (play_civ's Player.look_at + `move`); `coop.wobj bundle carry` on creatures of each game near it. Checks the game's
  object lists: the guest's own carried food stays 10 s, the host's carried food is in no hand of the guest's own
  creatures. Not every member is free: `carry` refuses a full hand; a member delivers its food to the mat by itself
  (the bundle is freed: `bundle take` only on listed bundles).
* `puppet_unground` (BUG-0683, ~2 min, creature fixture): `coop.puppet sink 0.69 0.7` puts the partner's puppet in the
  ground on the host; `coop.puppet agl` samples it. SPORE_FIX_OFF=creature.puppet_unground: stays under for 4 s (the
  game's own update put it back in 1 of 4 rounds).
* `keepalive_stall` (BUG-0687, ~1 min, creature fixture): `coop.stalltest 6` (an outside-frame command) makes the host's
  main thread run a message loop of its own for 6 s: no frames, the keepalive runs. The host's log must have one
  `keepalive: no frame for` line with return addresses into the game and one `keepalive: the main thread's windows`
  line; frames go on afterwards. As a normal (between-frames) command the loop runs inside the frame and the
  outside-frame tasks, the keepalive too, wait for its end: no line at all (the first try).
* BUG-0711: the monitor's `drawn` invariant leaves out partner lines whose `state_age` is over `drawn_age` (1.0 s) +
  the round trip: a copy without news can not be where the partner is now (multi4_creature_slow, lab 20261108-012301
  of fix/w46-k7-findings: the guest's link dead 5.5 s both ways, Carol drawn 16.5 m off at state_age 3.6 s); the dead
  link is the `link` / `puppet` invariants' (and "X is not responding"). `SPORE_MONITOR='{"drawn_age": -1}'` is the
  rule before it (gates_selftest f_drawn_stale / f_drawn_stale_off).

## The host's NPC ship shots on a guest (2026-11-08, fix/w46-k56-space)
<a id="testing-space-ship-shots"></a>

* `coop_space_shots.py` (~3 min, `space_empire` fixture, BUG-0677): both land on the colony planet 01460035 (in the
  star system the spawned pirates never fired in 45 s: NPC ships fight on planets), the host spawns 2 ambush pirates
  and 2 raiders and shoots each once (coop.fleet shoot: the game's TakeDamage, the pirates fight back on their own),
  then counts the projectile objects (default_beam_projectile 0x24630D7, default_tool_projectile 0x24270C5) in each
  game's noun list twice a second for 45 s (`coop.civ nouns <noun>` works in any stage). PASS 76 of 77 samples
  (lab 20261108-020511); SPORE_FIX_OFF=space.shots: 0 of 78 (FAIL, lab 20261108-021047).
* After the fix, play_space (lab 20261108-023555, 13 Space comparisons) had no `host_only:default_beam_projectile`;
  `guest_only` / `noun_only_guest:default_beam_projectile` in 2 of 13 (the copy starts and ends up to 0.2 s after the
  host's beam: relay lag, the class of the Civ shots in BUG-0657's notes). `All:state:time_of_day` on a Space planet in
  6 of 13 is BUG-0712 (open).
* Landing both takes 1-2 min on GOG 2024 lanes (play_space.Player.land_retry; the zoom does not land right after a
  load).

## The kept hold's limits (2026-11-08, fix/w46-k56-space)
<a id="testing-space-kept-hold-limits"></a>

* `coop_space_items_limits.py` (~5 min, `space` fixture, BUG-0679): the guest gets spice 0 x7 and an artifact
  (`coop.space trade cargo 5 10D5D4D3`: the game's artifact factory), the host spice 1 x5 and 3 more slots (`trade
  slots 3`); the guest leaves, gets 3 more spice while away, both games are started again (the host restart: one
  bottle per lane, so both), the restarted host gets its spice and slots again (start's `before_join`, which also
  switches SPORE_FIX_OFF off on the guest before it joins: a fix that acts during the join), the guest joins.
  PASS: the guest has spice x10, the artifact, no host spice, its own 10 slots.
* A guest's hold record (`<profile>/Spore/SporeCoop/holds/*.txt`) outlives the games: every test starts without one
  (twoinst forget_holds, also in the warm reset with `coop.space trade forget` on both games); a test that needs the
  record of its own earlier session passes `start(..., keep_holds=True)`.
