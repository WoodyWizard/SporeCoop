# Reporting a problem

Thanks for helping! One file from each player is usually all we need.

## 1. Get the report file

* **After a crash or a freeze:** SporeCoop makes the file by itself:
  `Spore\SporebinEP1\mods\logs\SporeCoop-report-<date-time>.zip`. The next time you start the game
  it tells you the file's name.
* **Anything else** (players out of sync, something does not work): press **F9** in the game, then
  **Report a problem**. It makes the same file and opens the folder with it selected.
* If both of you were playing, send the files of **both** players: the host's and the guest's.
  Each game's log says what that game saw; the two together show what went wrong between them.

The file holds the game build, Windows or Wine, memory and graphics card, the co-op session, the
logs of the last runs and the mod settings. Your player name is in it; the address you joined is
not, and the logs show other players' addresses only as their network class ("192.168.x.x",
"public #3F2A"). How much the log says is set in `mods\coop.ini` (`[coop] log_detail=1`, the
default; `0` = less, `2` = more, `coop.log detail N` in the F11 console); please keep the default
when you want to report a problem.

## 2. Spore's own crash files (optional)

When Spore itself catches a crash it writes two files into your Windows user folder (or the game
folder): `Exception Report <PC> <date> exception.txt` and `... minidump.mdmp`. They help too, but
they contain your Windows user name and PC name; send them only if you are fine with that.

## 3. Tell us what happened

Open an [issue](https://github.com/WoodyWizard/SporeCoop/issues/new/choose) ("Bug report or
crash") and write:

* the SporeCoop version (the bottom line of the F9 window or the main menu) of both players;
* the game (GOG, Steam, EA app) and the system (Windows, Linux);
* who had the problem (host or guest) and in which stage;
* what you did just before ("the host finished migrating to a new nest, then the guest's game
  closed");
* how often it happens.

**Mark the moment:** when something goes wrong while you play, type `/bug <what you see>` into the
co-op window's chat (F9), for example `/bug only one of us can eat`. The chat says "marked for the
report" and the log of **every** player in the session gets that line with your name and the
moment, so the right minute of a two-hour game is found at once in all the logs.

Screenshots help a lot for "players see different things": one from each player at the same
moment.

No GitHub account? Post the same in the [SporeCoop Discord](https://discord.gg/7Yre7u4b8x) (the files can be attached there
too).

Please check [known issues](known-issues.md) first, and update to the
[latest version](https://github.com/WoodyWizard/SporeCoop/releases/latest): many problems are
already fixed there.

## Reading the logs (for maintainers)
<a id="reporting-reading-logs"></a>

The report zip has `logs/sporekit.log` (this run of this game), `previous/` (the last runs) and
the other instances' logs. Every SporeCoop line starts with `[INF] coop: ` and a fixed prefix
(`log_detail=1`, from 0.7.4). Read the **host's and the guest's** logs side by side; the clocks are
each PC's own. A 2-player game writes about 0.3-0.6 MB an hour (measured in `coop_trace_log.py`),
so a 2 h game fits the 24 MB zip many times.

| prefix | when | what it answers |
|---|---|---|
| `trace:` | every 10 s while connected, and at once after a stage or world change | role and id, players, mode, world id and planet file, own position, hp, species, flags, DNA and the shared food / money of this game, host away, vote, the game's input lock, a pending own editor; then one `partner <id>` line each: its mode and world, distance, `same_place`, `puppet=shown` or `hidden: <why>`, its hp and species, the age of its last state, rtt, resends / packets, silence, pending reliable messages; once a minute the versions |
| `cell:` | with each trace in Cell | the own cell's level, size, hp, model and **diet** (`herb`, `carn`, `omni`), the food totals, the plants / meat / creatures **on the screen** and the nearest plant / meat, the pool size, mirrors (guest), knocked out, and what it did since the last line (ate, points, eats sent / applied / refused, bites); the guest's line ends with `host d=` and "outside the host's populated area" beyond 40 m (BUG-0174) |
| `creature:` | with each trace in Creature | NPCs simulated (host) or mirrored (guest) within 60 m, the nearest, and the nest distance |
| `tribe:` `civ:` `space:` | every 30 s in those stages | the first line of the stage module's counters |
| `eat:` | events | guest: `sent gone c<K> food=.. meal=..` (it ate the host's cell K), `sent kill`, `sent food` (points without a host cell), `host total ... -> +N here`; host: `from <id> gone c<K> ...: applied` or `refused (eaten first / not here)`, `from <id> kill`, `from <id> food`, `refused (this game is not in the Cell stage)`; both: `own cell ate +N` |
| `bite:` | events | guest `sent c<K> dmg=..`; host `from <id> c<K> ... hp a -> b`, `spared by the game's rules`, `refused: no such cell here` |
| `body:` | changes, and each minute in Creature / Cell | the own creation of the stage (`own=<key> '<name>' from choices.ini / newest own creation / the world's (host's) own`), the key used here, what the game has (`DIFFERENT` when they differ), the host's; `own avatar species A -> B`; `puppet of <id>`: the species / model its puppet was built from here and what the partner's own game reports |
| `vote:` | events | start (text, editor id, players), `player <id> ready=1 received`, `own ready=1 set`, guest `started / ended by the host`, `end: <reason>, ready a/b after N s` |
| `ui:` | events | every click on the co-op window's buttons (`click ready`, ...), clicks pressed under the game's input lock |
| `ko:` | events | own avatar dead / alive again, a partner dead / alive (from its state), the Cell knockout states |
| `stage:` | events | `<mode> <world> -> <mode> <world> '<planet>' (N s in the last)`: stage changes, editors, loads with their timings |
| `session:` | events | joins (with the partner's versions) and leaves (`left` or `timed out`) |
| `dna:` | events | the host's DNA total arriving on a guest, DNA kept while the host has no creature |
| `world:` `share` | (older lines) | the world share: save, send, receive, load steps |
| `net:` | (older lines) | the session: joins with `coop X, SporeKit Y, <game build> <exe hash>` of each partner, "versions differ", timeouts |

Repeated events (`eat:`, `bite:`, `dna:` ...) are written five times per kind and 10 s, then
summed up: `eat: own x37 more in the last 10 s (42 in all)`.

Where the answers to common reports are:

* **"Only one of us could eat" (Cell):** the `cell:` lines of the player who could not eat: no food
  on the screen (`screen: plants=0 meat=0`, nearest far away) with `host d=` over 40 m = the empty
  pool far from the host (BUG-0174); food on the screen but `ate=0` = the mouth (`diet=`) does not
  take it (a carnivore near plants); `eats_sent` on the guest without `from <id> gone ... applied`
  on the host = the messages did not arrive (look at the partner line's `silent=` and `pending=`).
* **"I spawn as the wrong creature / cannot change it" (Creature):** the `body:` lines of that
  player (where the own creation came from, `DIFFERENT`), `body: own avatar species A -> B` after
  the editor, and the other player's `body: puppet of <id>` line (which species its game used).
* **"I don't see my partner":** the `trace:` partner line: `same_place=0` (another world or mode)
  or `puppet=hidden: <why>`.
* **Votes that did not end / clicks that did nothing:** `ui: click ready`, `vote: own ready=1 set`
  on the guest, `vote: player <id> ready=1 received` on the host, `vote: end: <reason>`.

The session clock (`[s ...]` in every line, `tools/merge_logs.py` for one timeline of all
players), `/bug` marks, the sync self-check (`sync-check:`), `perf:`, `mem:`, `net:`, `settings:`
and `summary:` lines are explained in [SUPPORT.md](SUPPORT.md#support-reading), with a table of
where each answer is.
