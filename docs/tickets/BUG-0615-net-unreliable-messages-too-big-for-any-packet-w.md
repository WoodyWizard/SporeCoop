---
id: BUG-0615
title: Net: unreliable messages too big for any packet were dropped without a word
status: fixed
kind: tooling
stage: All
reported: 2026-11-06
source: BUG-0612 split (fix/w45-remote-bodies-2)
role: 
versions: 
fixed_in: 0.7.9
branch: fix/w45-remote-bodies-2
commits: db4b6090
tests: net_tests (make test)
signature:
docs:
related: BUG-0612, BUG-0425
---

## Report
Part of the BUG-0425 umbrella (G8 "log oversize unreliable messages"), split from BUG-0612. `Connection::build_packet` dropped an unreliable message that could never fit a packet (`used++; // can never fit any packet: drop it`) without a counter or a log line: a snapshot chunk that grew past one packet (more NPCs, longer rows) froze its objects on the other machine with nothing in any log.

## Root cause
Observed in the code (connection.cpp): the silent drop. Not seen in a player log (it could not be).

## Fix
Connection::send_unreliable refuses a message whose packet header (31 bytes) + message header (4) + payload is over kMaxPacket (1200) at once and counts it (`ConnStats::unrel_too_big`, last type and size); Session::flush logs the first one and then every 100th (`net: an unreliable message of type T (N bytes) is too big for a packet: dropped`), `coop.status`' session block shows `peer N: unreliable_too_big=`. Behaviour otherwise unchanged (it was and is dropped), so no switch.

## Verification
no-fail-run: diagnostics only (the message was dropped before and is dropped now). tests/net_tests.cpp test_unreliable_too_big: a 1300-byte message is counted with its type and size, the small one queued after it is delivered, a message of exactly the largest size that fits is sent.

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-remote-bodies-2, M2): opened from BUG-0612 and fixed (counted and logged).
- 2026-11-07: released in 0.7.9.
