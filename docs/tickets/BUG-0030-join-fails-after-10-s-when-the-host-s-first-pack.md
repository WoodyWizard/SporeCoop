---
id: BUG-0030
title: join fails after 10 s when the host's first packet is lost (WELCOME falsely acked)
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#8
role: host
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-net
commits: aa6d418
tests: net_tests test_lost_first_packet
signature:
docs: docs/04-netcode.md 4.6, docs/06-testing.md#testing-net-w3
related: #8
switch: net.first_ack
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/8 (full text, discussion).

Before a connection has received any packet, it still sends `ack = 0` in every header. The other side takes that as an acknowledgement of its own packet with seq 0. The host's first packet to a new guest carries `MSG_WELCOME`. If that packet is lost, the guest's next packet "acks" it, the host drops WELCOME from its resend queue, and the guest never gets it. The join then fails with "no answer from host (timeout)" after 10 s (wrong behaviour). On a link with p % loss, about p % of all joins fail this way (3 % on `mobile`, 10 % on `awful`).

### Where
- Header written with `remote_seq_` even when `have_remote_` is false (only the ack bits are zeroed): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/connection.cpp#L71-L76
- Initial values `local_seq_ = 0`, `remote_seq_ = 0`, `have_remote_ = false`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/connection.h#L93-L96
- Receiver accepts `p.seq == ack` without knowing whether the sender has received anything: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/connection.cpp#L191-L202
- Host queues WELCOME while handling the first HELLO, then flushes in the same `update()`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L319-L337
- Guest join timeout: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L555-L561

### How to reproduce
Harness (two `Connection` objects, drop exactly the host's first packet, no other loss):

```cpp
#include <cstdio>
#include <vector>
#include "net/connection.h"
#include "net/protocol.h"
using namespace net;
int main() {
    Connection host, guest;
    host.conn_id = guest.conn_id = 77;
    uint32_t now = 1000;
    std::vector<uint8_t> pkt;
    Writer hello; hello.u32(1);
    guest.send_reliable(MSG_HELLO, hello);
    int got_welcome = 0;
    guest.build_packet(now, pkt);
    host.on_packet(pkt.data(), pkt.size(), now, [&](uint8_t t, Reader&) {
        if (t == MSG_HELLO) { Writer w; w.u8(1); host.send_reliable(MSG_WELCOME, w); }
    });
    host.build_packet(now, pkt); // host packet seq 0 with WELCOME: lost
    for (int step = 0; step < 300; step++) { // 3 s without any loss
        now += 10;
        while (guest.build_packet(now, pkt))
            host.on_packet(pkt.data(), pkt.size(), now, [](uint8_t, Reader&) {});
        while (host.build_packet(now, pkt))
            guest.on_packet(pkt.data(), pkt.size(), now, [&](uint8_t t, Reader&) { got_welcome += t == MSG_WELCOME; });
    }
    printf("welcome delivered=%d host pending=%zu\n", got_welcome, host.pending_reliable());
}
```

Build: `g++ -std=c++20 -Imods/coop h.cpp mods/coop/net/connection.cpp mods/coop/net/crc32.cpp mods/coop/net/udp.cpp`

Observed: `welcome delivered=0 host pending=0`. WELCOME is never delivered, and the host thinks it was.

In game: join a host over a lossy link (`tools/tests/netem.py` profile `awful`, 10 % loss) about 20 times. Roughly one in ten joins should end in "no answer from host (timeout)".

### Expected
A connection that has received nothing acknowledges nothing. A lost WELCOME is resent after the RTO, like any reliable message.

## Root cause
**What happens**

1. The guest sends HELLO in its packet seq 0. The host creates the peer, `handle_msg` queues `MSG_WELCOME` (reliable id 0), and `flush()` sends host packet seq 0 with WELCOME in it.
2. Host packet seq 0 is lost.
3. The guest has still received nothing, so `have_remote_` is false and `remote_seq_` is 0. Its next keepalive or HELLO resend writes `w.u16(remote_seq_)`, which is `ack = 0`, with empty ack bits.
4. On the host, `on_packet` walks `sent_`, finds `p.seq == ack` (0 == 0) and calls `ack_packet`. That erases WELCOME (rel id 0) from `rel_out_`, so it is never resent.
5. Every later reliable message from the host (rel id 1, 2, ...) is buffered by the guest in `rel_in_`, waiting for the missing id 0 forever. After 10 s the guest gives up (`leave("join timeout")`). On the host, the peer stays accepted until the guest's BYE or a timeout.

The same thing happens to a REJECT in a lost first packet: the guest only sees a timeout instead of the reason. The client's own seq 0 is not affected, because the host only sends after it has received something.

### Confidence
CONFIRMED by harness (Connection level). Not verified: the failure rate in the game over netem. The full Session path (WELCOME is the first message in host packet 0, and the 10 s join timeout) was confirmed by reading.

## Fix
`Connection` starts its packet sequence at 1 (connection.cpp `build_packet`, `seq_started_`): a
peer that has received nothing writes ack 0 with no ack bits, which now matches none of our
packets until the sequence wraps (by then the peer has long received something). The lost
WELCOME stays in `rel_out_` and is resent after the RTO like any reliable message. No wire change:
an older peer still starts at 0 and keeps the old behaviour on its side only. `tools/fakepeer.py`
(the Python port) starts at 1 too. Switch: `coop.corefix first_ack 0` / `net::g_fix.first_ack`
(read when a connection sends its first packet).

## Verification
Unit test `test_lost_first_packet` (tests/net_tests.cpp, the harness of the report): the host's
first packet is dropped, then 3 s without loss.
- FAIL with `build/host/net_tests --nofix` (2026-10-28): `welcome delivered=0 host pending=0`.
- PASS with the fix (`make test`, also under ASan): `welcome delivered=1 host pending=0`.
Not run in game over netem (the 1-in-10 failure rate stays computed, not measured).

### Related
- docs/04-netcode.md 4.6 (handshake timings over netem: the 1-2 s on `awful` were measured on runs where packet 0 got through).
- Same root cause: any packet the sender transmits before the other side has received anything.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #8.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-net (wave 3).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: net.first_ack (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
