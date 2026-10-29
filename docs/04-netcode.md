# 4. Netcode

## 4.1 Topology

Star: the host is the server, guests connect to it over UDP (port 47810).
Guests don't talk to each other; the host relays player states.

The netcode is portable C++ (`mods/coop/net/`) with no game dependencies:
it builds both as a Windows DLL and **natively on Linux** (unit tests, `fakepeer`).

## 4.2 Packet

```
u32  magic   'SPCO'
u16  proto   protocol version (7)
u32  conn    connection id: the guest picks a new random one for every join (protocol 6)
u16  seq     packet number (for ack)
u16  ack     last received seq
u128 ackbits bitmask of the previous 128 (2 x u64, protocol 5)
u8   count   number of messages
[messages: u8 type, u8 channel, u16 len, u16 rel_id (reliable only), payload]
```

Protocol 7 added the Space extensions. A packet with another header protocol is dropped before
the handshake; `REJ_VERSION` applies to a mismatched protocol inside an otherwise valid `HELLO`.

Reliability uses a "seq/ack/ackbits" scheme: each reliable message remembers which
packets it was sent in, and is resent until a packet is acknowledged (RTO ≈ 1.5·RTT, min. 100 ms).
The receiver buffers reliable messages that arrive ahead of a missing one; the sender never sends a
message more than `kRelWindow` (2048) past its oldest unacknowledged one, so that buffer cannot
overflow. (Before 2026-10-23 the buffer held 1024 and silently dropped further messages while still
acknowledging their packets: after one message was lost a few times in a row on a bad link, the
dropped ones were never resent and the world transfer stalled for good.)

A packet whose `conn` differs from the connection's own is ignored. The host takes the id of the
first packet from a new address; a packet from a known address with a new id is a new connection
(the guest left and joined again through a NAT or proxy that kept the mapping) and replaces the old
peer, and ids of dropped peers are remembered, so late or duplicated packets of an old connection
cannot recreate it or mix into the new one.

Channels:
* `UNRELIABLE`: states (snapshots), where the latest value matters more than delivery;
* `RELIABLE_ORDERED`: session events, abilities, damage, chat, file chunks.

Each frame a peer gets one flush: unreliable messages first (the own `PLAYER_STATE` at the front),
then due reliable ones. The ack field covers 128 packets (protocol 5), so a flush sends up to 128
packets on a short round trip (LAN, < 40 ms) and 42 on the internet - that paces the reliable bulk
(a 3.8 MB world: ~3 MB/s); while unreliable messages remain it goes on for up to 64 more packets,
then the rest of the unreliable queue is dropped (stale by the next frame). Big snapshots (cell pool: one ~900-byte chunk per 14 cells) must stay below that.

## 4.3 Messages

| Type | Channel | Contents |
|---|---|---|
| `HELLO` | R | `u16` protocol version, `u32` exe build-id, `str8` nickname (no mod version: peers of the same protocol are not told apart) |
| `WELCOME` | R | `u8` your player id, `u32` world id (save hash), `u8` count, then `{u8 id, str8 name}` per player (no host tick) |
| `REJECT` | R | `u8` reason (1 version, 2 no free slots, 3 different exe), `str8` text |
| `PLAYER_JOIN/LEAVE` | R | JOIN `u8 id, str8 name`; LEAVE `u8 id, u8 reason` (no appearance: the look comes from `CREATION` and the species in the state) |
| `PLAYER_STATE` | U | id, tick, position (3×f32), orientation, velocity, movement flags, health, species, **mode** and **place** (protocol v2: a puppet is visible only in the same mode and place) |
| `ANIM` | R | animation played by the player's creature (jump, attack, social); the host relays it |
| `NPC_SPAWN` | R | net-id, species key (ResourceKey), scale, flags (dead / "family" / impressed), position, HP |
| `NPC_STATE` | U | 10 Hz, batches of ~25: net-id, position, orientation, velocity, HP, flags |
| `NPC_DESPAWN` | R | net-id: left the 110 m radius (+20 m hysteresis) or disappeared |
| `DAMAGE_REQ` | R | guest → host: my hit on an NPC (net-id, damage, type) |
| `DAMAGE` | R | host → guest: an NPC hit you (damage, type, attacker net-id) |
| `DNA` / `DNA_ADD` | R | shared DNA total from the host / gain earned by a guest (also the DNA a guest earned alone while the host was in the main menu) |
| `SPECIES` | R | host → guests: the creature stage's species relationship table (per species: relation, impressed, killed), on change and every 10 s; the guest writes it into its own and refreshes its goal cards |
| `SOCIAL` | R | guest → host: a social ability on a mirrored NPC (net-id, ability type); host → guest: rounds done / needed; host → all: the result (impressed or refused, reaction animation, DNA); any → host: a goal card's ally / extinction reward (species, outcome, DNA), paid once |
| `RES` / `RES_ADD` | R | shared resource (u8 id: 1 tribe food, 2 civilization money, 3 empire money; f64): total from the host / change on a guest |
| `PING_MARK` | R | "look here" marker: author id, point, mode; the host forwards it to the others |
| `WORLD_TIME` | U | once per second: quaternion of the sky object (time of day) |
| `WORLD_OFFER/FILE/CHUNK/DONE` | R | transfer of the host's save in 1 KB chunks, at most 160 unacknowledged messages (600 when the round trip is under 40 ms) |
| `CHAT` | R | id, UTF-8 text |
| `PING/PONG` (8, 9) | U | unused: nothing sends `PING` (the host still answers one with a `PONG`); the round trip in `coop status` and the lobby is `rtt_ms`, averaged from the packet acks |
| `ABILITY` (11) | - | reserved, never sent or handled |
| `BYE` | U | reason; sent in three packets on leave (protocol 6: a reliable one waited behind any lost earlier message) |

CORRECTION (2026-10-28, fix/w3-net, BUG-0144): the rows `HELLO`, `WELCOME`, `PLAYER_JOIN/LEAVE`
and `PING/PONG` described fields and messages the protocol never had (a mod version, a host tick,
an appearance, a PING sender); they now list what the `write()` methods in `net/protocol.h` send.

## 4.4 Rates and smoothing

* State sending: 20 Hz (every 50 ms), no more than once per frame.
* The receiver keeps a snapshot buffer and renders the puppet with a **100 ms** delay
  (linear position interpolation, orientation slerp); when data runs short it
  extrapolates by velocity for up to 250 ms, then "freezes".
* Divergence > 8 m → the puppet teleports, otherwise it smoothly catches up.
* In game, the puppet moves through the locomotion system (a "walk to point" command
  targeting the interpolated position), so the game plays the step animations itself;
  orientation/position are corrected directly at low speed.

## 4.5 Entity identifiers

* Players: `player_id` 0 = host, 1..3 = guests.
* NPCs (M2): the host assigns a `net_id` when an object first enters any guest's
  interest radius, and keeps an `object ↔ net_id` map. On the guest, its own NPCs are disabled
  (AI doesn't tick, objects are hidden), and puppets are created from `NPC_SPAWN` instead.

## 4.6 Session and timeouts

* Handshake: `HELLO` → `WELCOME`/`REJECT`; `HELLO` is resent like any reliable message for up to 10 s.
* Leave: `BYE`; the host drops the peer at once. A lost `BYE` costs nothing on a rejoin (the new
  connection id replaces the peer), otherwise the peer times out.
* ~ superseded (2026-10-28): No packets for 5 s → "connection unstable", 15 s → disconnect.
* ~ superseded (2026-10-28): `coop status` shows RTT, packet loss, and each player's state.
* CORRECTION (2026-10-28, fix/w3-net, BUG-0069): no packets for 30 s (`Session::timeout_ms`,
  session.h: the game can block that long while saving or loading) → disconnect ("connection to
  host lost (timeout)" on a guest, the peer dropped on the host). A guest without `WELCOME` after
  10 s gives up ("no answer from host (timeout)"); the host drops a peer it has not accepted after
  10 s. There is no "unstable" state. `coop status` prints per peer `rtt`, `last` (ms since the
  last packet), `sent`, `recv`, `acked`, `resend` and `pending` (reliable messages not acked
  yet), not a loss figure.
* The first packet of a connection has seq 1 (BUG-0030): a peer that has received nothing writes
  ack 0 with no ack bits, which acknowledges nothing (with a first packet of seq 0 it acknowledged
  the host's lost `WELCOME`, and the join timed out).

### Measured over netem (2026-10-23, tribe save of 3.8 MB, `coop_rejoin_net.py`, lane 2)

| Link | handshake (join to `together`) | world transfer (host "sharing" to guest "received") | rejoin until the resent world is loaded |
|---|---|---|---|
| plain (localhost) | 0.8 s | 1.0 s | 9.5 s |
| `mobile` (120 ± 60 ms, 3 % loss) | 0.8 s | 8 s | 17 s |
| `awful` (250 ± 120 ms, 10 % loss) | 1-2 s | 16 s | 25 s |

* The host saves for ~2.6 s before sending, the guest loads for ~4 s after receiving.
* Over a long round trip the transfer is window bound: 160 messages in flight (world.cpp), host
  rtt ~450 ms during the transfer on `awful`, ~330 chunks/s. A bigger window was tried in a
  simulation of `awful` (unit test `test_bulk_bad_link` without the hole): 240-600 messages cut the
  time to 7-10 s but 2-4x the resends were spurious (jitter of ± 120 ms reorders more packets than
  the 128-packet ack window covers, so their acks never come), and a cumulative "next reliable id"
  ack did not reduce them (most spurious resends are buffered behind a hole). Left at 160.

## 4.7 NAT

M1–M3: direct IP (UDP port forwarding on the host or a VPN network). Later: a rendezvous server
for UDP hole punching (a small service you can host yourself).

## Messages from other players (2026-10-27, fix/w1-net)
<a id="netcode-untrusted"></a>

Every handler treats a message from another player as untrusted: it reads the whole message and
checks it (`r.ok()` after the last field, counts and indices against what this machine has, sizes
against the buffer they describe, numbers finite and in range, player ids against the session)
before it changes anything, and a snapshot chunk that does not parse does not count for its cycle.
A message is used and passed on only when it fits the player it came from. Shared checks: `mods/coop/net/validate.h` (unit tests
`tests/validate_tests.cpp`); in game: `coop_net_robust.py`
([06-testing](06-testing.md#testing-net-robust)).

## Big messages, late states, clocks and ids (2026-10-28, fix/w2-net)
<a id="netcode-w2"></a>

* **Reliable messages above one packet are split** (BUG-0034). A reliable payload fits a packet up
  to 1163 bytes (1200 minus the 31-byte header and 6 bytes of message header). A bigger one is cut
  into pieces of that size by `Connection::send_reliable`, each its own reliable message with flag
  bit 2 ("more pieces follow") on all but the last; the receiver joins them in order (reliable
  delivery is ordered) and delivers one message. Limit `kMaxReliable` = 1 MB per message (a sender
  refuses more, a receiver drops what goes past it). Before, such a message was never sent, blocked
  every reliable message after it and made every flush send a full burst of empty packets (5000
  packets/s on a LAN). Old peers (protocol 7, no change of `kProtoVersion`) deliver the pieces as
  separate messages, which their parsers reject; they stalled on such a message anyway.
* **A guest ignores the state of a player not in its roster** (BUG-0109). The host relays a guest's
  state unreliably behind the frame's snapshot chunks, so the last state of a leaving guest could
  arrive after the reliable `PLAYER_LEAVE` and made a ghost partner (in Space a "partner" that
  disabled instead of killed and repaired the ship). `player_join` also drops an old entry of the
  id (its puppet, its snapshot buffer).
* **Clock offset over a window** (BUG-0133, `net::ClockOffset` in interp.h): local time minus sender
  time of the fastest snapshots, the minimum of 1 s buckets over the last 3 s. A faster snapshot is
  taken at once; a higher minimum (the latency rose) is followed at 0.25 s per second, so the render
  clock slows down and never jumps back. After +300 ms of latency puppets and NPC proxies
  interpolate again after ~3.4 s (before: 50 s of 20 Hz steps).
* **Ship and vehicle snapshots carry the host's time** (BUG-0122): a u32 `now_ms()` after the last
  entry of each `MSG_UFO_STATE` / `MSG_CIV_STATE` chunk (`net::snapshot_time` finds it by the entry
  size; older guests ignore it, older hosts do not send it and the guest falls back to the arrival
  time). `VelocityEstimate::update(p, now, sender_s)` divides by the host's interval and ages the
  snapshot in host time mapped by a `ClockOffset`. One packet 75 ms late: 0.1 units of error at
  100 u/s (before 22).
* **Net ids of host objects** (BUG-0154): `net::alloc_id` (net/ids.h) skips ids in use and 0, for
  ships (squadron.cpp), civ vehicles (civ.cpp, both places) and tribe members (tribe.cpp).
* **A player's creations leave with the player** (BUG-0076): `creations::on_player_leave` forgets the
  owner's bundles (host store, received pieces, applied keys), so the next player of the id is shown
  with its own creature or the world's species, and later joiners do not get the old one.
* Fix switches (tests): `coop.corefix big_reliable|state_roster|offset_window|sender_time|id_reuse|creations_leave 0|1`;
  unit tests: `net_tests --nofix` runs the wave 2 cases on the code before the fixes (they fail).

## Decoder fuzzing and the transport seam (2026-10-28, fix/w4-census)
<a id="netcode-fuzzing"></a>

`tests/fuzz_decoders.cpp` runs in `make test` (AddressSanitizer + UBSan, fixed seeds, about 3 s;
compiling it takes longer than running it). It feeds everything portable that parses bytes from
another player or from the internet:

* every `protocol.h` struct and the MSG_NPC_STATE batch: write/read round trip, every strict prefix
  rejected, 6000 mutations each (bit flips, interesting bytes, truncation, insertion, deletion,
  0xFFFF lengths); an accepted mutation must encode and decode to itself;
* `read_lobby` (both `fix` modes), `chunk_add` (MSG_CREATION pieces: shuffled, repeated, forged
  offsets and totals), `snapshot_time`, the UTF-8 helpers and `str8`/`str16`;
* RefPack, DBPF (`Package`, `index_info`/`parse_index`, `find_keys`, `metadata_names`, header
  fields aimed at), the update pack and the release JSON / SHA256SUMS parsers;
* random and mutated datagrams into `Connection::on_packet` and into a host and a guest `Session`.

The Session runs over `Session::set_transport()` (`net/transport.h`: the UDP socket by default, a
queue in the test) with `Session::clock` (nullptr: `now_ms()`), so no socket and no real time is
involved. `fuzz_decoders --seed S --iters N` runs longer; built with `-DFUZZ_LIBFUZZER
-fsanitize=fuzzer,address` (clang) the same file is a libFuzzer target (first byte picks the decoder).
Checked that it catches real mistakes: removing the `o + 3 > n` check of the RefPack 3-byte code or
the `r.left() < len` check of `on_packet` makes it fail with a heap-buffer-overflow.

Not covered: the `on_message` handlers of the game modules (tribe, civ, cell, space ...) read their
payloads next to game calls and do not build without the game; their common checks live in
`validate.h` (tests/validate_tests.cpp).

Found on the way: `Reader::raw(nullptr, 0)` (an empty payload read into an empty vector's `data()`)
passed nullptr to memcpy (undefined behaviour, harmless on the compilers we use): it skips the copy
now. A RefPack stream that lacks only its final bare stop code (0xFC) is accepted, because the
decoder stops at the declared size; that is intended (the data is complete).

## HELLO / WELCOME caps and the rejoin token (2026-10-28, fix/w5-net)
<a id="netcode-hello-caps"></a>

Protocol 7 stays. HELLO (`proto u16, build u32, name str8`) and WELCOME (`your_id u8, world u32,
players`) get an extension appended after their 0.7.2 fields, which every older reader ignores
(their `read()` never checked for trailing bytes): `u32 caps`, then the payload of each set bit in
ascending bit order (BUG-0219).

| bit | name | payload | meaning |
| --- | --- | --- | --- |
| 0 | `CAP_SPLIT` | none | joins split reliable messages (the BUG-0034 receiver) |
| 1 | `CAP_TOKEN` | `u64` | HELLO only: the rejoin token (BUG-0235); never in WELCOME or anything relayed |
| 2 | `CAP_VERSION` | HELLO: `str8`; WELCOME: `str8` + `u8 n` + n x (`u8 id`, `str8`) | the versions (`coop X, SporeKit Y, <game build> <exe hash 8>`) of the sender and, in WELCOME, of the players the host knows (0.7.4, feat/w7-trace; see #netcode-versions-masks) |

* A message without the extension reads as caps 0: 0.7.2.x, 0.7.3.0 and fakepeer. A short payload
  means "absent", not a failed read. Unknown bits and trailing bytes are ignored. A new field takes
  the next bit and appends its payload after those of the lower bits; never insert a field before
  the extension. `tests/fuzz_decoders.cpp` checks both shapes (`hello_welcome_ext`, and a prefix
  that still holds the base fields must be read).
* `Connection::peer_split` (false until the handshake) comes from the guest's HELLO on the host and
  from WELCOME on the guest. A reliable message above one packet (`Connection::kMaxSingle` = 1163
  bytes of payload) goes out in pieces only to such a peer; to an older one it is not sent and
  counted (`too_big_old`, a line `older peer N: too_big_old=.. frag_lost=..` after the session
  block of `coop.status`); pieces from a peer that never announced splitting are dropped
  (`frag_lost`). Chat lines to an older peer are cut between characters to `kMaxSingle - 3` bytes,
  per peer (the host's relay too). One notice names an older player at each join.
* What a mixed session loses (0.7.2.x / 0.7.3.0 with a newer version): chat lines above about 1160
  bytes arrive cut, a Creature species table of 42 or more records does not reach the older player.
  0.7.3.0 is "older" here too (it has no caps), although it could join pieces.
* Rejoin token: `mods/coop/rejoin.cpp`, `<AppData>\SporeCoop\rejoin-<instance>-<slot>.txt` in the
  profile's AppData (lanes: in the lane's bottle), the first slot the game can open exclusively, kept
  open for the process. A join sends the token in the file (a crash left it) or writes a fresh one;
  a clean leave, the session end and the unload empty it. The host's HELLO handler drops an accepted
  entry with the same token that has been silent for 2 s before the full check and before WELCOME
  (PLAYER_LEAVE(old) goes out before PLAYER_JOIN(new)). Without a token nobody is replaced.
  Switches: `coop.fix net.peer_caps`, `net.rejoin_token` (off = the 0.7.3.0 name rule of BUG-0209),
  `core.rejoin_name` (off = never replace), `pool.meals_trim`. `net_tests --off <peer_caps|rejoin_token|addr_label|update_paths>`
  turns one switch off.

## Peer versions and masked addresses in logs (2026-10-29, feat/w7-trace)
<a id="netcode-versions-masks"></a>

* `CAP_VERSION` (bit 2 of the HELLO / WELCOME caps): `Session::version` goes out in HELLO; the host
  keeps it per peer (`Peer::version`, `PlayerInfo::version`), sends its own and the known players'
  in WELCOME and appends the newcomer's to PLAYER_JOIN (`str8` after the 0.7.2 fields: older
  readers ignore it; an empty or missing one is "unknown (pre-0.7.4)"). A payload that does not
  read leaves the message readable without versions. Both sides log every partner's versions on a
  join (`net: player N 'X' joined from ...: coop ...`, `session: ...`), `coop.status` lists them
  (`version N 'X': ...`), the trace repeats them once a minute; a different SporeCoop or SporeKit
  version is a warning line, a different SporeCoop version also a notice in the chat. The exe build
  itself must match already (REJ_BUILD).
* Addresses: `Endpoint::masked()` / `net::mask_ip` (net/ipmask.h) for every log line, `coop.status`
  and the chat log that goes into problem reports: loopback as is, LAN / VPN ranges by class
  (`192.168.x.x:47810`, `10.x.x.x`, `172.N.x.x`, `100.64.x.x (VPN/CGNAT)`), a public address as
  `public #ABCD` (16 bits of FNV-1a: tells players apart, not where they are). A host name looked
  up for a join is logged by its length only. The full address stays in the session (and in the
  co-op window, which the player sees). Tests: `net_tests` `test_peer_versions`, `test_ip_mask`;
  `fuzz_decoders` PLAYER_JOIN with and without the version.
