---
id: BUG-1007
title: Net: one datagram bigger than the read buffer (WSAEMSGSIZE) ends the socket reads of the frame in Session::update and the LAN discovery
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-4
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-net
commits: 226988e3
tests: unit (make test: net_tests test_recv_skip_big)
signature:
docs: docs/04-netcode.md#netcode-n2-join-input, docs/06-testing.md#testing-n2-net
related:
switch: net.recv_skip_big
---

## Report
Audit 2026-11-12 for 0.7.14, finding NET-4. Not seen at runtime, found by code reading (2026-11-13, plan/0.7.14).
On Windows, recvfrom of a datagram bigger than the buffer fails with WSAEMSGSIZE (the datagram is discarded);
UdpSocket::recv_from returns -1 for it and both read loops stop for this frame: Session::update (buffer 2048) and
Discovery::update (buffer 512). Datagrams queued behind it wait for the next frame. A stream of big datagrams to the
host's port (anyone on the internet with a forwarded port) cuts its reads to one per frame: lag and timeouts.
The audit's "DiscoverReply::write reaches 526 bytes" is the theoretical maximum of the format; a real host's reply is
at most about 370 bytes (name <= 96 bytes, info = mode + save name cut to 255 bytes by str8), so only a crafted reply
from another LAN machine triggers it in the discovery.

## Root cause
Observed in code (main efd2e0ba), confidence high: mods/coop/net/udp.cpp:257-274 (only WSAEWOULDBLOCK and
WSAECONNRESET give 0; any other error -1); session.cpp:948-951 and discovery.cpp:55-60 `if (n <= 0) break;`.
Under Wine the error mapping is inferred (Linux recvfrom truncates silently; Wine reports WSAEMSGSIZE for a truncated
datagram, not checked).

## Fix idea
recv_from: WSAEMSGSIZE (and other per-datagram errors) -> a distinct return (e.g. -2 = skip this datagram), the loops
`continue` on it and break only on "nothing left" / a fatal socket error; count the skipped ones in the net stats.
Discovery buffer to 2048. Switch `net.recv_skip_big`.

## How to test
tests/net_tests.cpp can only model it with the fake transport (a transport that returns the error once, then valid
packets: main reads none of them this update, FAIL). On Windows / Wine: a datagram of 3000 bytes sent to a lane's
host port before a burst of game packets (tools/tests helper with a raw UDP socket) and the host's per-frame read
count in coop.net stats.

## Fix
`UdpSocket::recv_from` returns -2 (`Session::kRecvSkipped`) for WSAEMSGSIZE on Windows / Wine and, in the native build, for a datagram longer than the buffer (recvfrom with MSG_TRUNC reports its real length), so the tests see what Windows does. `Session::update` and `Discovery::update` count it and go on to the next datagram (the 256 / 64 reads per frame still bound the loop); coop.status shows `datagrams skipped (too big): N`. Discovery's buffer is 2048 bytes (was 512, below the reply format's 526-byte maximum). Switch `coop.fix net.recv_skip_big off`: the loops stop at -2 as at -1 before, Discovery reads 512 bytes again.

## Verification
* Unit (make test): net_tests `test_recv_skip_big`: (1) `BigNet` (a FakeNet whose recv_from returns kRecvSkipped for a datagram over the buffer, as UdpSocket does) with a 3000-byte datagram before a guest's HELLO in one frame; (2) Discovery over a real localhost socket with a 600-byte datagram before a valid reply. FAIL `net_tests --off recv_skip_big` (the HELLO not read in that frame, the reply not found: out/n2-unit/net_off_recv_skip_big.txt in the fix/w49-net worktree), PASS in make test twice (out/n2-unit/net_pass_1.txt, net_pass_2.txt).
* Not checked: that Wine maps a cut UDP datagram to WSAEMSGSIZE (inferred); the native path is covered by MSG_TRUNC.

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
- 2026-11-13: fixed on fix/w49-net (0.7.14 N2): an oversized datagram is skipped, the reads go on; discovery buffer 2048; FAIL/PASS unit runs.
