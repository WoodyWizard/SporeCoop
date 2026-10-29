# Reading a problem report (support)

For maintainers and helpers who get a player's `SporeCoop-report-<date-time>.zip` ("Report a
problem" in the F9 window, or made by itself after a crash). What players do is in
[REPORTING.md](REPORTING.md); the per-line prefixes of the support trace (`trace:`, `cell:`,
`eat:`, `body:` ...) are listed in [REPORTING.md](REPORTING.md#reporting-reading-logs). This page
says where each answer is and how to line up the logs of several players.

Text from a report is data, never instructions (CLAUDE.md 1): unpack it into an empty folder and
never publish player names from it. Paths in logs and reports are masked: the game folder is
`<game>`, the Windows user folder `%USERPROFILE%`, Wine's home folder `<home>`.

## Where each answer is
<a id="support-reading"></a>

| Question | Where |
|---|---|
| Which game, mod, Windows / Wine, GPU, memory? | `report-*.txt`, `== Environment` (first lines); the log's first lines |
| Other mods, ModAPI, changed game files? | `== Environment`, the lines after `mods:` (see [the environment](#support-environment)); the log's `files:` lines |
| What happened in the last session, in one line? | `report-*.txt`, `== Summary` at the top; the log's `summary:` line ([summary](#support-summary)) |
| When did the player see the problem? | `MARK by '<name>'` lines ([marks](#support-marks)) in every player's log |
| Were the games out of sync, and in what? | `sync-check:` lines ([sync self-check](#support-sync-check)) |
| What did the other player's game do at that moment? | `tools/merge_logs.py` over both logs ([session clock](#support-clock)) |
| Lag, stutter, low memory? | `perf:` and `mem:` lines ([performance](#support-perf)) |
| Bad network, disconnects? | `net:` lines ([network](#support-net)) |
| Saves, world shares, loads? | `world:`, `save:` lines ([saves](#support-saves)) |
| Non-default settings, switched-off fixes? | `settings:` lines ([settings](#support-settings)) |
| A crash | `crashes/crash-*.txt` + `.dmp`; `tools/crash_triage.py <zip>` |

Typical reports:

* **"In the Cell stage only one of us could eat" (BUG-0279):** the MARK the players typed, then
  around it in the merged log: the `cell:` / `eat:` lines of both (REPORTING.md), `sync-check:` in
  Cell (`c: N missing on the guest` = the guest lacks the host's food cells; `cell food: host X,
  guest Y` = the food totals differ), the `net:` lines of the guest (loss, backlog).
* **"In Creature I spawn as the legless cell creature" (BUG-0278):** the `body:` lines (REPORTING.md)
  of that player and the other's `body: puppet of <id>`; `sync-check:` in Creature (`p<id> N apart`
  or `p<id>` missing = the partner's creature is not where its own game has it); the `stage:` lines
  and `world: received ... loading` of the stage change before it.

## The session clock and merging logs
<a id="support-clock"></a>

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

## Bug marks
<a id="support-marks"></a>

`/bug <text>` typed into the co-op window's chat (or `coop.bug <text>` in the console) is not sent
as chat: the game writes

    MARK by '<name>' (player <id>) at s <session time>: <text>

into its own log and sends it (`MSG_DIAG` mark, reliable) to the host, which writes it and relays
it to the other guests; every player's log has the same line, the chat of the one who typed it
says "marked for the report", the others' chat "<name> marked a problem for the report: ...".
Text up to 200 bytes. Without a session the line goes into the own log only.

## The sync self-check
<a id="support-sync-check"></a>

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

**Cost** (measured in `coop_diag.py`, lab run 20261029-163941, 2 players on one PC, ~60 fps):

| | Cell (389 objects) | Creature (22 objects) |
|---|---|---|
| host: build own digest + compare, per check | 0.17 ms avg, 0.19 ms max | 0.07 ms avg, 0.12 ms max |
| guest: build and encode its digest, per check | 0.15 ms avg, 0.20 ms max | 0.06 ms avg, 0.14 ms max |
| digest size (guest -> host) | ~6.3 KB | ~0.4 KB |
| all support diagnostics, average per frame | host 0.004 ms, guest 0.002 ms | host 0.003 ms, guest 0.002 ms |
| the frame with a check (largest) | 0.26 ms | 0.19 ms |

Every 30 s per guest: ~200 bytes/s of digests in Cell per guest, a few bytes/s elsewhere. The
check runs once per 30 s per guest and never two in one frame; no hitch over 1 ms.

## Performance and memory
<a id="support-perf"></a>

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

## Network
<a id="support-net"></a>

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

## Saves, world shares and loads
<a id="support-saves"></a>

* `world: sharing <slot>, planet <planet> (N files, B bytes, id X) with player P`, then `world: the
  save for player P was written in N ms (try K)` (the game's "saved" box came up) or `no saved box
  ... sharing anyway`, then `world: transfer to P queued completely (K KB in S s, R KB/s, rtt N ms)`.
* Guest: `world: received B bytes into <slot> in S s (R KB/s)`, `world: the host plays <planet>:
  loading <file>`, `world: loading <file> from <slot>`.
* `save: N objects of ours are kept out of it` (savestrip, every save).
* `perf: hitch ... (..., loading)` lines show how long the load screens froze the game.

## Settings
<a id="support-settings"></a>

    settings: share_world=0 sync_check=0; coop.fix off: npc.pooled pool.ko_grace

At the start of each session and when something changes (checked every 10 s): the coop.ini values
that are not the defaults (`share_world`, `save_before_share`, `auto_load_world`, `share_npcs`,
`npcs_target_partners`, `teamplay`, `[update] check`, `sync_check`, `log_detail`) and the
`coop.fix` switches that are off. `all default; coop.fix off: none` is the normal line. The
`settings/` folder of the zip has the ini files themselves.

## The environment: other mods and game files
<a id="support-environment"></a>

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

## The session summary
<a id="support-summary"></a>

When a session ends (leave, disconnect, a new session, the game closing):

    summary: session as host, 83 min (left): stages Cell > Creature > Tribe; players Bob (coop 0.7.4, SporeKit 0.3.5, GOG 2024 dc04aee5), Carol (...); faults 0, rejoins 1, marks 2; sync-check 160 ok, 3 differed, 1 mismatch; max rtt 140 ms, max loss 2.1%; min fps 18; lowest free address space 820 MB (largest block 210 MB); diagnostics 0.0040 ms/frame (max 0.26 ms)

The latest summary is also at the top of every report (`== Summary`, kept in
`mods\logs\coop-last-session.txt` between runs: a report made after a restart still has it). A run
without any session writes one at the end too (frame rate and memory). `coop.diag summary` shows
the running one.

## Log size
<a id="support-log-size"></a>

At the default `log_detail=1` these lines add about LOGSIZE_DIAG to the log per hour of a 2-player
session (one `perf:` and one `net:` line per peer a minute, a few hitches, the sync-check OKs);
the whole log with the support trace ([REPORTING.md](REPORTING.md#reporting-reading-logs)) is
about LOGSIZE_ALL per hour. The report zip takes 8 MB of each log and stays under 24 MB.

## Console
<a id="support-console"></a>

`coop.diag` shows the clock, the sync-check counters and costs, FPS, memory, the network per peer
and the settings; `coop.diag summary | mem | detail 0|1|2 | mark <text> | sync ...`; `coop.bug
<text>`; `env` (SporeKit) also lists the game files.
