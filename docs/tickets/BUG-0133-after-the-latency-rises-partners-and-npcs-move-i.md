---
id: BUG-0133
title: after the latency rises, partners and NPCs move in steps for about 50 s
status: fixed
kind: desync
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#112
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-net
commits: a8341cf
tests: tests/net_tests.cpp test_interp_latency_rise
signature:
docs: docs/04-netcode.md#netcode-w2
related: #112
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/112 (full text, discussion).

`SnapshotBuffer` maps the sender's clock to the local clock with the smallest offset it has seen (`local_now - sender_time`). When the latency goes **up**, it forgets the old minimum by only 0.2 ms per received snapshot. That is 4 ms per second at the 20 Hz player-state rate. After a latency rise of 300 ms the buffer renders past its newest snapshot for about **50 s**. During that time every partner puppet and NPC proxy from that sender is extrapolated from the newest snapshot instead of interpolated, and it moves in 20 Hz steps.

### Where
- Offset update: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/interp.h#L48-L62
- Render time and the extrapolation path: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/interp.h#L71-L110
- Used for players (`coop.cpp` `player_state` callback, `buf.push(s, H->time_seconds())`) and for NPC proxies (`npc.cpp` `MSG_NPC_STATE`, `px.buf`, delay 0.15 s).

### How to reproduce
Harness against the real `mods/coop/net/interp.h` (build: `g++ -std=c++17 -O2 -I<repo>/mods/coop lat.cpp -o lat && ./lat`):

```cpp
#include "net/interp.h"
#include <cstdio>
using namespace net;
int main(){
  SnapshotBuffer b; // delay 0.10 s, max_extrap 0.25 s (defaults)
  double lat=0.03, first_bad=-1, recovered=-1;
  for(int k=0;k<20*200;k++){ // 200 s of player states at 20 Hz
    double sent=1000+k*0.05;
    if(sent>1010) lat=0.33;   // one-way latency +300 ms from t=10 s on (for example bufferbloat)
    PlayerState s; s.time_ms=(uint32_t)(sent*1000); s.pos={(float)(k*0.25),0,0}; s.vel={5,0,0}; s.flags=1;
    double recv=sent+lat;
    b.push(s,recv);
    // sample() renders at t = now - offset - delay; it extrapolates when t > newest, i.e. age > delay
    bool extrap = b.age(recv) > b.delay + 1e-9;
    if(sent>1010 && extrap && first_bad<0) first_bad=sent;
    if(first_bad>0 && !extrap && recovered<0) recovered=sent;
  }
  printf("latency +300ms at t=10s: extrapolating from %.1f s, interpolation back at %.1f s (%.0f s later)\n",
         first_bad-1000, recovered-1000, recovered-first_bad);
}
```

Output on main 563f72d:

```
latency +300ms at t=10s: extrapolating from 10.0 s, interpolation back at 60.0 s (50 s later)
```

In game: two instances on one machine, with `tc qdisc add dev lo root netem delay 15ms` during a creature session. Then `tc qdisc change dev lo root netem delay 315ms`: every packet now takes 300 ms longer one way. The partner puppet keeps moving in steps for about 50 s.

### Expected
The buffer adapts to a new, higher latency within a few seconds, and the puppet is interpolated again 2 to 3 s after the latency settles.

## Root cause
**What happens**

`offset_` drops at once to any smaller offset. A larger one only raises it by 0.0002 s per push: `offset_ = off < offset_ + 0.0002 ? off : offset_ + 0.0002`. `sample()` renders at `t = now - offset_ - delay`.

When the one-way latency rises by L:
- `t` lies `L - delay` past the newest snapshot;
- sampling uses the extrapolation branch: the newest snapshot moved on with its velocity, capped at 0.25 s;
- between two snapshots the drawn position does not follow a smooth path, it jumps at every arrival.

To get back to interpolating, `offset_` must rise by `L - delay` at 4 ms/s. For L = 300 ms and delay 100 ms that takes 50 s. For L above 350 ms the 0.25 s extrapolation cap is also exceeded, so the puppet trails behind as well.

A typical trigger is the host's uplink filling up while it sends a world to a joining third player (bufferbloat), or Wi-Fi roaming.

### Confidence
Confirmed with the harness above (real `interp.h`). A second reviewer verified it. Severity low: this is a quality problem, not a desync, but it is visible on unstable links.

## Fix
Fixed on fix/w2-net (a8341cf). `net::ClockOffset` (interp.h) keeps the minimum offset of 1 s
buckets over the last 3 s. A smaller offset is taken at once; a higher window minimum is followed
at 0.25 s per second, so the render clock runs slower for a moment instead of jumping back.
`SnapshotBuffer` (player puppets, NPC proxies) uses it. Switch: `coop.corefix offset_window 0`.

The review's suggestions were:

Track the minimum offset over a sliding window: keep the minimum of each 1 s bucket over the last 3 s, and use the minimum of the buckets. A sustained rise is then adopted after about 3 s, while single fast packets still pull the offset down at once. Alternatively, raise the forget rate whenever every sample of the last second was above `offset_ + delay`.

## Verification
Unit test `test_interp_latency_rise`: 20 Hz states, +300 ms one-way latency at 10 s. With the fix
the buffer interpolates again 3.4 s after the rise, the drawn path never steps back (largest step
back 0.00); with steady 0-40 ms jitter no sample renders past the newest snapshot. `--nofix`:
50.0 s (FAIL). test_interp and test_interp_gap still pass.

Planned in the review:

New case `test_interp_latency_rise` in `tests/net_tests.cpp`:
1. 20 Hz states, latency 30 ms for 10 s, then 330 ms.
2. `CHECK(age(now) <= delay)` (interpolating again) within 5 s of the rise.

On main it FAILS: recovery after 50 s. With the windowed minimum it PASSES in about 3 s. The existing `test_interp` / `test_interp_gap` must still pass.

### Related
#31 (millisecond clock wrap in the same buffer), #12 (big reliable messages, also a source of queueing delay), tracking issue #82.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #112.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-net (wave 2, netcode); tests above FAIL without the fix and PASS with it.
- 2026-10-28: released in 0.7.3.0.
