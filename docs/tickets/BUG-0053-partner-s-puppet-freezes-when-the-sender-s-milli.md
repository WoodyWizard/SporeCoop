---
id: BUG-0053
title: partner's puppet freezes when the sender's millisecond clock wraps (49.7 days uptime)
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#31
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-net
commits: aa6d418
tests: net_tests test_interp_wrap
signature:
docs: docs/06-testing.md#testing-net-w3
related: #31
switch: net.time_wrap
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/31 (full text, discussion).

Player states carry `time_ms`, the sender's `now_ms()`: steady-clock milliseconds since boot, cut to 32 bits. This value wraps to 0 after 2^32 ms (49.7 days of uptime). The receiver's snapshot buffer drops every snapshot whose time is not newer than the last one, so after the wrap all of that player's states are dropped. The other players see the puppet frozen in place until the session is restarted (wrong behaviour, rare: it needs a PC with about 49.7 days of uptime, counting sleep where QueryPerformanceCounter keeps running).

### Where
- `now_ms()` truncates to `uint32_t`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/udp.cpp#L40-L44
- `send_state` stamps it: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L243-L254
- `SnapshotBuffer::push` compares it as a non-wrapping double: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/interp.h#L48-L62

### How to reproduce
Harness:

```cpp
#include <cstdio>
#include "net/interp.h"
using namespace net;
int main() {
    SnapshotBuffer b;
    PlayerState s;
    s.time_ms = 0xFFFFFF00u; s.pos = {1, 0, 0}; b.push(s, 100.0);
    s.time_ms = 0x00000100u; s.pos = {5, 0, 0}; b.push(s, 100.5); // 512 ms later, after the wrap
    PlayerState out; b.sample(101.0, out);
    printf("x=%g\n", out.pos.x);
}
```

Build: `g++ -std=c++20 -Imods/coop h.cpp`. Observed: `x=1` (the post-wrap snapshot was dropped). Expected: about 5.

In game: only on a PC whose uptime crosses 2^32 ms during a session. Not reproduced in game.

### Expected
Snapshot order is decided with wrap-aware arithmetic (`(int32_t)(time_ms - last_time_ms) > 0`), so the puppet keeps moving across the wrap.

## Root cause
**What happens**

```cpp
double st = s.time_ms / 1000.0;
if (!snaps_.empty() && st <= snaps_.back().t)
    return; // old or duplicate
```

1. Before the wrap, `snaps_.back().t` is about 4 294 967 s.
2. After the wrap, `time_ms` restarts near 0, so `st` is a few seconds and `st <= back().t` is true for every new snapshot. All of them are dropped.
3. `sample()` keeps returning the last pre-wrap snapshot, extrapolated for at most 250 ms. The puppet stops, and `age()` grows without limit.
4. The buffer is only cleared when the remote entry is erased (that player leaves). The host relays a guest's states with the guest's own `time_ms`, so every other player sees the freeze too.

The `Connection` timers are not affected: they use unsigned differences (`now - last_recv_ms`), which survive the wrap.

### Confidence
CONFIRMED by harness (unit level). Not verified: whether Wine's QueryPerformanceCounter starts at boot or at Wine start (if at Wine start, the uptime that matters is Wine's).

## Fix
`SnapshotBuffer::push` (net/interp.h) keeps a 64-bit unwrapped sender time: each snapshot adds
`(int32_t)(time_ms - last_raw)` and a difference <= 0 is dropped as old or duplicate. A last time
of 0 is the "no time" of an NPC spawn (npc.cpp pushes `time_ms = 0` before the first state), so any
time after it is newer (with a signed difference, a host up for more than 24.8 days would have had
every NPC state dropped after the spawn). `clear()` resets it. Switch: `coop.corefix time_wrap 0`
/ `net::g_fix.time_wrap`. NPC batches use the same buffer and are covered by it.

## Verification
Unit test `test_interp_wrap` (the report's harness plus an overtaking pre-wrap snapshot and the
NPC spawn's time 0 followed by a 28-day host time).
- FAIL with `net_tests --nofix`: `x=1 (5 expected)`, age check fails.
- PASS with the fix: `x=5`.
Not reproduced in game (needs 49.7 days of uptime).

### Related
- docs/04-netcode.md 4.4 (rates and smoothing).
- Other `time_ms` users: `NpcState` batches (`u32 time_ms`) in npc.cpp; not checked whether they use the same comparison.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #31.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-net (wave 3).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: net.time_wrap (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
