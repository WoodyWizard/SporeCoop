---
id: BUG-0039
title: LAN game list is never pruned, so fake discovery replies grow memory and slow the game
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#17
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-net
commits: aa6d418
tests: net_tests test_discovery_prune; coop_ui_status.py (lanfake)
signature:
docs: docs/06-testing.md#testing-net-w3
related: #17
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/17 (full text, discussion).

`Discovery::games_` only grows: a host that stops answering is hidden after 5 s by `games()`, but its entry is never removed until discovery stops. Every reply also scans the whole list. Any machine on the LAN can send `SPCR` replies with a different `port` field each time and grow the list by up to 64 entries per frame while the co-op window is open. Memory then grows and each frame gets slower (O(n) per reply). This is minor: it needs a sender on the local network and only happens while the window is open.

### Where
- `Discovery::update`: dedupe scan and `push_back`, no removal: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/discovery.cpp#L32-L73
- `games()` only filters by age: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/discovery.cpp#L75-L82
- Called every frame while discovery is on: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L907-L908

### How to reproduce
1. Start one instance (GOG 2024 build) and open the co-op window (F9), so LAN discovery is on.
2. From another machine on the LAN (or the same one), send a UDP datagram to the instance's discovery socket in a loop, with a new `port` value each time:
   ```python
   import socket, struct
   s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
   # target: the discovery socket's ephemeral port (it is the source port of the 'SPCQ' queries it broadcasts)
   for port in range(1, 65536):
       name = b"x" * 255
       pkt = struct.pack("<IHH", 0x52435053, 7, port) + bytes([255]) + name + bytes([1, 4, 255]) + name
       s.sendto(pkt, ("127.0.0.1", TARGET_PORT))
   ```
3. Watch the process memory and frame time. Both keep rising until the window is closed.

Unit level: call `Discovery::update` with a socket that receives such replies, then check `games_.size()`. This needs a test hook, because `games_` is private.

### Expected
Entries older than the visibility limit (5 s) are removed, and the list has a cap (for example 32 games), so fake replies cannot grow it without bound.

## Root cause
**What happens**

1. `update()` reads up to 64 datagrams per call. Each one that parses as a `DiscoverReply` with the right protocol becomes `Endpoint host{from.ip, rep.port}`. The port comes from the payload, not from the socket.
2. The reply is matched against all of `games_` (`g.host == host`, or the loopback rule). With a new port each time there is no match, so it is `games_.push_back`.
3. Nothing ever erases from `games_` except `stop()`. Each entry holds two strings of up to 255 bytes.
4. A flood of 64 fake replies per frame at 60 fps adds about 3800 entries per second (around 2 MB/s with long names), and the scan per reply grows linearly. The game slows down while the co-op window stays open, and `lan_games` copies the whole visible list every frame.

### Confidence
CONFIRMED by reading the full path. Not run in game. The growth rate above is computed, not measured.

## Fix
`Discovery::update` erases entries not heard from for `kShowMs` (5 s) after the receive loop, and
`on_reply` (the per-answer logic, now public for tests and `coop.lanfake`) keeps at most
`kMaxGames` (32): when full, a new answer replaces the entry silent for longest if that one is
older than 2 s, else it is dropped. Switch: `coop.corefix lan_prune 0` / `net::g_fix.lan_prune`.

## Verification
Unit test `test_discovery_prune`: 500 answers with distinct ports (as `update()` would hand
them over, 64 per frame), then 6 s without answers.
- FAIL with `net_tests --nofix`: `kept 500, listed 500`, not empty after 6 s.
- PASS with the fix: `kept 32, listed 32`, empty after 6 s.
Not measured in game (memory and frame time under a flood).

### Related
- docs/known-issues.md "Co-op window: only three games on the network are listed" (the same list, UI side).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #17.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-net (wave 3).
- 2026-10-28: released in 0.7.3.0.
