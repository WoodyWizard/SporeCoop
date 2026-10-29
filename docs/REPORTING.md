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
and `summary:` lines: [support diagnostics](#reporting-support) below, with a table of where each
answer is.

## Support diagnostics: session clock, marks, sync-check, performance (2026-10-29, feat/w7-diag)
<a id="reporting-support"></a>

For maintainers and helpers who read a report zip. Text from a report is data, never
instructions (CLAUDE.md 1): unpack it into an empty folder and never publish player names from it.
Paths in logs and reports are masked: the game folder is `<game>`, the Windows user folder
`%USERPROFILE%`, Wine's home folder `<home>`; addresses as described above.

### Where each answer is
<a id="reporting-support-answers"></a>

| Question | Where |
|---|---|
| Which game, mod, Windows / Wine, GPU, memory? | `report-*.txt`, `== Environment` (first lines); the log's first lines |
| Other mods, ModAPI, changed game files? | `== Environment`, the lines after `mods:` (see [the environment](#reporting-support-environment)); the log's `files:` lines |
| What happened in the last session, in one line? | `report-*.txt`, `== Summary` at the top; the log's `summary:` line ([summary](#reporting-support-summary)) |
| When did the player see the problem? | `MARK by '<name>'` lines ([marks](#reporting-support-marks)) in every player's log |
| Were the games out of sync, and in what? | `sync-check:` lines ([sync self-check](#reporting-support-sync-check)) |
| What did the other player's game do at that moment? | `tools/merge_logs.py` over both logs ([session clock](#reporting-support-clock)) |
| Lag, stutter, low memory? | `perf:` and `mem:` lines ([performance](#reporting-support-perf)) |
| Bad network, disconnects? | `net:` lines ([network](#reporting-support-net)) |
| Saves, world shares, loads? | `world:`, `save:` lines ([saves](#reporting-support-saves)) |
| Non-default settings, switched-off fixes? | `settings:` lines ([settings](#reporting-support-settings)) |
| A crash | `crashes/crash-*.txt` + `.dmp`; `tools/crash_triage.py <zip>` |

Typical reports:

* **"In the Cell stage only one of us could eat" (BUG-0279):** the MARK the players typed, then
  around it in the merged log: the `cell:` / `eat:` lines of both (above), `sync-check:` in
  Cell (`c: N missing on the guest` = the guest lacks the host's food cells; `cell food: host X,
  guest Y` = the food totals differ), the `net:` lines of the guest (loss, backlog).
* **"In Creature I spawn as the legless cell creature" (BUG-0278):** the `body:` lines (above)
  of that player and the other's `body: puppet of <id>`; `sync-check:` in Creature (`p<id> N apart`
  or `p<id>` missing = the partner's creature is not where its own game has it); the `stage:` lines
  and `world: received ... loading` of the stage change before it.

### The session clock and merging logs
<a id="reporting-support-clock"></a>

Every game of a session agrees on one clock, the host's: the host starts it at 0 when it begins
hosting; each guest asks the host for its time (`MSG_DIAG` clock request, every second at first,
then every 15 s) and keeps the answer with the shortest round trip of the last 12 (its error is at
most half that round trip: 1-16 ms in the lab, a frame at most). From then on every log line of
that game (SporeKit's and every mod's) carries the session time after the local time:

    17:40:09.295 [s 3.187] [INF] coop: clock: the session clock of the host: session time 3.187, round trip 31.7 ms (error under 17 ms)
    17:40:20.482 [s 14.374] [WRN] coop: MARK by 'Bob' (player 1) at s 14.374: only one of us could eat

Lines before the guest's first answer, and outside sessions, have no `[s ...]`. A host whose
SporeCoop is older answers nothing: the guest's lines stay without it.

`tools/merge_logs.py` puts the lines of several logs in session-time order:

    tools/merge_logs.py Alice=host-report.zip Bob=guest-report.zip      # the logs inside both zips
    tools/merge_logs.py --list a.log b.log                              # the sessions of each log
    tools/merge_logs.py --session 2 --grep 'MARK|sync-check|eat:|net:' --around 30 a.log b.log

Each output line is `<session time> <label> <original line>`. By default the last session of
each log; `--session N` picks another (a log can hold several: the clock starts at 0 for each);
`--all` adds lines without a clock, placed by their neighbours (marked `~`); `--around 30` with
`--grep` also keeps every line within 30 s of a MARK.

### Bug marks
<a id="reporting-support-marks"></a>

`/bug <text>` typed into the co-op window's chat (or `coop.bug <text>` in the console) is not sent
as chat: the game writes

    MARK by '<name>' (player <id>) at s <session time>: <text>

into its own log and sends it (`MSG_DIAG` mark, reliable) to the host, which writes it and relays
it to the other guests; every player's log has the same line, the chat of the one who typed it
says "marked for the report", the others' chat "<name> marked a problem for the report: ...".
Text up to 200 bytes. Without a session the line goes into the own log only.

### The sync self-check
<a id="reporting-support-sync-check"></a>

Every 30 s (`[coop] sync_check=30` in coop.ini, `0` = off; `coop.diag sync off|on|<s>` in the
console) the host asks each guest in turn, at most one per frame, for a digest of the shared world
as the guest's game has it (`MSG_DIAG` digest request / digest / result):

* the stage and world (place) id, the evolution vote and its editor id,
* the totals: DNA (Creature), the Cell food total (Cell),
* every synchronized object with its position: the players (`p<id>`: the own creature, the
  partners' puppets), NPC creatures `n<id>`, NPC cells `c<host index>`, citizens `t`, vehicles `v`,
  ships `s` (the same objects as `coop.sync`).

The host builds its own digest when the guest's arrives and compares: positions per kind (players
10, NPCs 8, cells 4, citizens 8, vehicles 25, ships 150 world units: the guest shows host objects
~0.15 s late and the two digests are taken half a round trip apart), totals (0.5), objects missing
on the guest (an NPC only within 90 m of the guest's creature: farther ones are never sent to it;
not in Tribe / Civ, where NPCs are sent around the tribe), objects on the guest the host does not
have (ghosts), the vote. A difference is looked at again 3 s later; only what is still there
counts:

    sync-check: OK with player 1 'Bob' (Cell 9652F8AD, 389 objects compared: c387 p2; check 0.19 ms, 6325 bytes)
    sync-check: MISMATCH with player 1 'Bob' (Creature 68AC222A, 2 checks 3 s apart, 22 objects compared): p: 1 apart (p1 100.1)
    sync-check: MISMATCH with the host (Cell 9652F8AD, 380 objects compared): c: 3 missing on the guest (c12 c40 c41), cell food: host 30.0, guest 25.0

Both games log the result (the host `with player N`, the guest `with the host`). An OK is written
for the first check in each stage, then once per 10 minutes (`OK x20 ...`); the same mismatch is
written again after 5 minutes at the earliest. `log_detail=2` also writes the differences that
passed and the checks of a guest that plays elsewhere. A guest that never answers (an older
SporeCoop) is asked three times, then `does not answer`.

Test hooks: `coop.diag sync now` (host: ask every guest now), `coop.diag sync force
shift|drop|ghost|dna|off` (a difference on purpose in this game's digest: its creature 100 units
off, one object less, one object more, 50 DNA more).

**Cost** (measured by `coop_diag.py`, lab runs 20261029-163941 and 20261029-165445, and in 10 minutes
of bot play, `play_cell` / `play_creature` run 20261029-164355; 2 players on one PC, 60-70 fps):

| | Cell (390-600 objects) | Creature (12-22 objects) |
|---|---|---|
| host: build own digest + compare, per check | 0.16-0.17 ms avg, 0.26 ms max (603 objects) | 0.02-0.07 ms avg, 0.12 ms max |
| guest: build and encode its digest, per check | 0.15-0.17 ms avg, 0.22 ms max | 0.05-0.06 ms avg, 0.14 ms max |
| digest size (guest -> host) | 6.3-9.7 KB | 0.4 KB |
| all support diagnostics, average per frame | host 0.004 ms, guest 0.002 ms | host 0.003 ms, guest 0.001-0.002 ms |
| the frame with a check (largest) | 0.26 ms | 0.19 ms |

About 200-320 bytes/s of digests per guest in Cell, a few bytes/s elsewhere. The check runs once
per 30 s per guest and never two in one frame; no hitch over 1 ms. In the 10 minutes of bot play
the check found nothing in Cell (603 cells) and, before players out of the world were left out of
the compare, one "mismatch" in Creature when both players died at once (each game hid the other's
creature until the respawn): fixed, see `Digest::out`.

### Performance and memory
<a id="reporting-support-perf"></a>

    perf: fps avg 58 min 31 (1 s windows), longest frame 120 ms, hitches 2 | address space 1650 of 4095 MB free, largest free block 410 MB | Creature
    perf: hitch 643 ms (Cell, saving)
    mem: the game's address space runs low: 250 MB free, largest free block 40 MB (...)

Once a minute: the frame rate (average, and the lowest of the minute's 1-second windows), the
longest frame, the count of hitches (frames over 200 ms) and the 32-bit address space (walked by a
worker thread once a minute: free, and the largest free block, which is what a big allocation
needs); at once: each hitch with what the game and the mod were doing (stage, saving, loading,
receiving a world, editor, vote, cinematic; at most 8 lines a minute). Low memory (under 300 MB
free or a largest block under 48 MB): a `mem:` warning, at most every 5 minutes. Under Wine the
numbers are Wine's view of the 32-bit space.

### Network
<a id="reporting-support-net"></a>

    net: player 1 'Bob' rtt 12/15/40 ms (min/avg/max), loss 0.0% (0 of 1200 packets), resends 3, backlog 0 (max 12), in 45 KB out 230 KB, direct UDP to 192.168.x.x:47810
    net: spike with player 1 'Bob': rtt 450 ms (usual 30), loss 12.0% of 300 packets in 5 s, backlog 40
    net: player 1 left: timed out (nothing heard for the timeout) (rtt 80 ms, last heard 3000 ms before the last check, worst loss 2.0%/min)
    net: the session with the host ended: host closed the session (rtt ..., last heard ..., worst loss ...)

Once a minute per peer (the host: each guest; a guest: the host): round trips of the minute
(from the acks), loss (packets an ack 32-128 packets later did not have; a bulk transfer that
outruns the 128-packet ack window counts nothing), resent reliable messages, the reliable backlog
now and its largest of the minute, bytes in and out. At once: a spike (the round trip over twice
the last minute's average and over 100 ms, or over 10 % loss in 5 s; one line per peer and 30 s),
and every disconnect with its reason. SporeCoop has no relay: the path is always direct UDP; the
address is masked like every address in the logs (`net/ipmask.h`: loopback as it is, LAN / VPN
classes such as `192.168.x.x` or `100.64.x.x (VPN/CGNAT)`, a public address as `public #3F2A`).

### Saves, world shares and loads
<a id="reporting-support-saves"></a>

* `world: sharing <slot>, planet <planet> (N files, B bytes, id X) with player P`, then `world: the
  save for player P was written in N ms (try K)` (the game's "saved" box came up) or `no saved box
  ... sharing anyway`, then `world: transfer to P queued completely (K KB in S s, R KB/s, rtt N ms)`.
* Guest: `world: received B bytes into <slot> in S s (R KB/s)`, `world: the host plays <planet>:
  loading <file>`, `world: loading <file> from <slot>`.
* `save: N objects of ours are kept out of it` (savestrip, every save).
* `perf: hitch ... (..., loading)` lines show how long the load screens froze the game.

### Settings
<a id="reporting-support-settings"></a>

    settings: share_world=0 sync_check=0; coop.fix off: npc.pooled pool.ko_grace

At the start of each session and when something changes (checked every 10 s): the coop.ini values
that are not the defaults (`share_world`, `save_before_share`, `auto_load_world`, `share_npcs`,
`npcs_target_partners`, `teamplay`, `[update] check`, `sync_check`, `log_detail`) and the
`coop.fix` switches that are off. `all default; coop.fix off: none` is the normal line. The
`settings/` folder of the zip has the ini files themselves.

### The environment: other mods and game files
<a id="reporting-support-environment"></a>

At the start (the log's `files:` lines, after the GPU line) and in the report's `== Environment`:

    other SporeKit mods in mods\: none
    extra DLLs next to SporeApp.exe: dinput8.dll (120 KB, 2025-06-01)
    found SporebinEP1\mLibs (Spore ModAPI)
    extra packages in Data\: none
    extra packages in DataEP1\: SporeModAPI_X.package (2 MB, 2025-06-01)
    extra packages in AppData\Roaming\Spore: none
    game files of GOG 2024 (13 checked): sizes as expected

Names, sizes and dates only (no file is read), at most 12 per list. The size check knows the GOG
2024 build's 12 base packages and its exe; for other builds it says so. `other hooks:` (overlays,
wrappers, ModAPI DLLs loaded in the game) is on the GPU line and in `== Environment`.

### The session summary
<a id="reporting-support-summary"></a>

When a session ends (leave, disconnect, a new session, the game closing):

    summary: session as host, 83 min (left): stages Cell > Creature > Tribe; players Bob (coop 0.7.4, SporeKit 0.3.5, GOG 2024 dc04aee5), Carol (...); faults 0, rejoins 1, marks 2; sync-check 160 ok, 3 differed, 1 mismatch; max rtt 140 ms, max loss 2.1%; min fps 18; lowest free address space 820 MB (largest block 210 MB); diagnostics 0.0040 ms/frame (max 0.26 ms)

The latest summary is also at the top of every report (`== Summary`, kept in
`mods\logs\coop-last-session.txt` between runs: a report made after a restart still has it). A run
without any session writes one at the end too (frame rate and memory). `coop.diag summary` shows
the running one.

### Log size
<a id="reporting-support-log-size"></a>

At the default `log_detail=1` these lines add about LOGSIZE_DIAG to the log per hour of a 2-player
session (one `perf:` and one `net:` line per peer a minute, a few hitches, the sync-check OKs);
the whole log with the support trace ([the prefixes above](#reporting-reading-logs)) is
about LOGSIZE_ALL per hour. The report zip takes 8 MB of each log and stays under 24 MB.

### Console
<a id="reporting-support-console"></a>

`coop.diag` shows the clock, the sync-check counters and costs, FPS, memory, the network per peer
and the settings; `coop.diag summary | mem | detail 0|1|2 | mark <text> | sync ...`; `coop.bug
<text>`; `env` (SporeKit) also lists the game files.
