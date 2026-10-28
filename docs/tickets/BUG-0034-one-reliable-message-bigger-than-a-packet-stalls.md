---
id: BUG-0034
title: one reliable message bigger than a packet stalls all reliable traffic and floods empty packets
status: fixed
kind: desync
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#12
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-net
commits: a8341cf, 29fa3f6
tests: tests/net_tests.cpp test_big_reliable; tools/tests/coop_chat_long.py (suite chat_long)
signature:
docs: docs/04-netcode.md#netcode-w2, docs/06-testing.md#testing-net-w2
related: #12
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/12 (full text, discussion).

A reliable message whose payload does not fit into one datagram (more than about 1163 bytes) can never be sent. It also blocks every reliable message queued after it, for the rest of the session. While blocked, the connection keeps reporting "something is due", so every frame `flush()` sends the full burst (42 or 128) of empty packets to that peer. Unreliable traffic keeps the connection alive, so nothing times out: NPC spawns, damage, chat, DNA, the world transfer and everything else reliable silently stop for that peer (wrong behaviour, with a packet storm). Concrete triggers: `coop say` with a long text, and `MSG_SPECIES` with more than 41 species.

### Where
- Due check and `any_rel_due`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/connection.cpp#L46-L65
- The reliable loop `break`s on the first due message that does not fit: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/connection.cpp#L103-L121
- `send_reliable` accepts any size: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/connection.cpp#L29-L36
- `flush()` sends up to `burst` packets while `build_packet` returns true: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L481-L494
- Trigger 1, `coop say` without a length limit (`str16` allows 4000 bytes): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1921-L1934 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/buffer.h#L31-L36
- Trigger 2, the species table in one message (up to 512 records of 28 bytes): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/social.cpp#L75-L87 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/social.cpp#L516-L519

### How to reproduce
Harness:

```cpp
#include <cstdio>
#include <vector>
#include "net/connection.h"
#include "net/protocol.h"
using namespace net;
int main() {
    Connection a, b;
    uint32_t now = 1000;
    Writer big; for (int i = 0; i < 1180; i++) big.u8('x');
    Writer small; small.u32(5);
    a.send_reliable(MSG_CHAT, big);
    a.send_reliable(MSG_NOTICE, small);
    int got = 0, pkts = 0;
    std::vector<uint8_t> pkt;
    for (int step = 0; step < 200; step++) { // 2 s of 10 ms frames
        now += 10;
        for (int i = 0; i < 128 && a.build_packet(now, pkt); i++) {
            pkts++;
            b.on_packet(pkt.data(), pkt.size(), now, [&](uint8_t t, Reader&) { got += t == MSG_NOTICE; });
        }
        while (b.build_packet(now, pkt))
            a.on_packet(pkt.data(), pkt.size(), now, [](uint8_t, Reader&) {});
    }
    printf("small delivered=%d, packets sent by a in 2 s=%d, a pending=%zu\n", got, pkts, a.pending_reliable());
}
```

Build: `g++ -std=c++20 -Imods/coop h.cpp mods/coop/net/connection.cpp mods/coop/net/crc32.cpp mods/coop/net/udp.cpp`

Observed: `small delivered=0, packets sent by a in 2 s=25600, a pending=2`. Note that a `while (a.build_packet(...))` loop, as used in `test_reliability`, never ends in this state.

In game (2 instances, any stage): host and guest connected, then on the guest `coop say` followed by about 1200 characters of text. After that, the host gets no further chat, DNA, damage requests or social acts from that guest. `coop status` on the guest shows `pending` growing and `sent` rising by about 42 per frame.

Creature stage: a planet whose species table (`coop.social species`) has more than 41 records stalls every guest within 10 s of the first `send_table`. The number of records on a real planet was not checked.

### Expected
An oversized reliable message is either split by the sender, or refused loudly at `send_reliable` (a log line, not queued). Messages after it still flow, and an idle connection sends at most a keepalive.

## Root cause
**What happens**

1. `kMaxPacket` is 1200 and the header is 31 bytes. A reliable message needs 6 bytes of message header, so payloads over about 1163 bytes never fit.
2. In `build_packet`, the oversized message is due (`sends == 0`, or the RTO has passed), so `any_rel_due = true` and a packet is built.
3. In the reliable loop:
   ```cpp
   if (w.size() + 6 + m.payload.size() > kMaxPacket || count == 255)
       break;
   ```
   The loop stops at the oversized message, so no later message is ever reached. Once the earlier messages are acked, the oversized one is always first and every packet has 0 reliable messages.
4. `build_packet` still returns true, because `any_rel_due` stays true. `flush()` loops `for (i < burst && build_packet(...))`, so 42 packets per frame (128 on LAN) go to that peer, each a bare 31-byte header.
5. The receiver keeps getting packets, so no timeout fires. The session looks connected, but nothing reliable arrives any more.

Unreliable messages that cannot fit are dropped (`used++`, a known and documented behaviour, docs/stability/tribe.md, civ.cpp chunking). Reliable ones are not.

### Confidence
CONFIRMED by harness (stall and storm). The `coop say` trigger is CONFIRMED by reading (`c_say` has no limit; the UI edit box limits chat to 120 characters). The `MSG_SPECIES` trigger is PLAUSIBLE: the 41-record threshold is exact, but a real planet's table size was not checked. Other unchunked reliable senders with a count-prefixed list (space_contracts, space_rivalry, space_map, spacecoop) were not checked.

## Fix
Fixed on fix/w2-net (a8341cf). `Connection::send_reliable` splits a payload above 1163 bytes
(`kRelPiece`) into reliable pieces of one packet each; all but the last carry wire flag bit 2
("more pieces follow"). The receiver (`Connection::deliver_reliable`) joins the pieces in order
(reliable delivery is ordered) and delivers one message; it never collects more than
`kMaxReliable` (1 MB), the sender refuses bigger messages (`stats.too_big`). Every queued message
now fits a packet, so the due check can no longer cause the empty-packet burst, and chat and
`MSG_SPECIES` need no chunking of their own. No protocol version bump: a peer on protocol 7 gets
the pieces as separate messages, which its parsers reject (it stalled on such a message anyway).
Switch: `coop.corefix big_reliable 0` (and `net_tests --nofix`) restores the old path.

The review's suggestions were:

- In `Connection::send_reliable`: if `payload.size() > kMaxPacket - kHeader - 6`, log and drop the message (or assert in debug builds), so it can never block the queue.
- In `build_packet`: compute `any_rel_due` only over messages that can fit, so a stuck message cannot cause the empty-packet storm.
- Callers: chunk `MSG_SPECIES` (or a generic reliable fragmenting helper), and clamp chat to about 1000 bytes in `Session::send_chat`.
- Keep the old behaviour behind `coop.fix big_reliable off` so the lab can show the test catches it.

## Verification
Unit test `test_big_reliable` (tests/net_tests.cpp): a 1180-byte and a 30000-byte reliable
message, then a 4-byte one, with 0 % and 20 % loss. With the fix: all three delivered intact,
nothing pending, 127 / 73 packets in 3 s. `net_tests --nofix`: nothing delivered, 3 pending,
38400 packets in 3 s (FAIL).

In game, `coop_chat_long.py` on lane 2: `--without-fix` FAIL (neither the 1500-character line nor
the next one reached the host, guest pending=8, 5280 packets/s to the host);
with the fix PASS twice (both lines, pending=0, 33 packets/s).

Planned in the review:

- `tests/net_tests.cpp`: the harness above as a test that CHECKs `got == 1` and `pkts` below a few hundred. It fails on main and passes with the fix.
- Lab: `tools/tests/coop_chat_long.py` (2 instances). The guest sends a 1500-character `coop say`, then a normal one. Assert that the host's chat log shows the second message, and that a DNA gain on the guest still reaches the host's DNA counter (the game's value).

### Related
- docs/stability/tribe.md: the unreliable version of the same limit (24 citizens per chunk were more than `kMaxPacket`).
- civ.cpp chunk budget comment (a larger message is dropped by the connection for good).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #12.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-net (wave 2, netcode); tests above FAIL without the fix and PASS with it.
- 2026-10-28: released in 0.7.3.0.
