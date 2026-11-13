---
id: BUG-1007
title: Net: one datagram bigger than the read buffer (WSAEMSGSIZE) ends the socket reads of the frame in Session::update and the LAN discovery
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-4
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
