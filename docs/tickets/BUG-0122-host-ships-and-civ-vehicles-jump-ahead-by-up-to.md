---
id: BUG-0122
title: host ships and civ vehicles jump ahead by up to 40 m on the guest after one late packet
status: fixed
kind: desync
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#101
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-net
commits: a8341cf, 9afc74d
tests: tests/net_tests.cpp test_velocity_jitter
signature:
docs: docs/04-netcode.md#netcode-w2
related: #101
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/101 (full text, discussion).

Ship snapshots (`squadron.cpp`) and civilization vehicle snapshots (`civ.cpp`) carry no velocity and no sender time. The guest estimates the velocity with `net::VelocityEstimate`, which divides the distance between two snapshots by the gap between their **arrival** times. Network jitter goes straight into the speed estimate. The mirror is then extrapolated with that speed for up to 0.43 s, so a single late packet makes the host's ship overshoot by about 22 units at 100 u/s, and a bunched pair by about 40 units.

### Where
- Velocity from receive time: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/interp.h#L123-L144
- Ships, update and use: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L290 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L341
- Civ vehicles, update and use: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1471 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1548
- Host send interval 0.1 s, snapshots without a time: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L183-L229

### How to reproduce
The harness uses the real `mods/coop/net/interp.h`. Save it as `est.cpp` and build with `g++ -std=c++17 -O2 -I<repo>/mods/coop est.cpp -o est && ./est`:

```cpp
#include "net/interp.h"
#include <cstdio>
#include <random>
#include <vector>
using namespace net;
int main(){
  const float S=100.f; // units/s, straight line along x; truth at local time t is S*(t-1000)
  { // case 1: one packet 75 ms late in an otherwise perfect 0.1 s stream (30 ms path)
    VelocityEstimate e; double maxerr=0;
    for(int k=0;k<40;k++){
      double sent=k*0.1; double recv=1000+sent+0.03 + (k==20?0.075:0.0);
      e.update({(float)(S*sent),0,0}, recv);
      for(double t=recv; t<recv+0.1; t+=1/60.0){
        Vec3 a=e.at(t); double err=std::fabs(a.x-S*(t-1000));
        if(k>=5 && err>maxerr) maxerr=err;
        if(k>=19&&k<=24 && t==recv) printf("k=%d recv=%.3f vel=%.1f err=%.1f\n",k,recv,e.vel.x,err);
      }
    }
    printf("single 75ms-late packet: max error %.1f units (%.2f s of flight)\n",maxerr,maxerr/S);
  }
  { // case 2: packet 20 arrives in the same frame as 21, packet 22 25 ms after them
    VelocityEstimate e; double maxerr=0, recv[40];
    for(int k=0;k<40;k++) recv[k]=1000+k*0.1+0.03;
    recv[20]=recv[21]; recv[22]=recv[21]+0.025;
    for(int k=0;k<40;k++){
      e.update({(float)(S*k*0.1),0,0}, recv[k]);
      double nxt = k<39? recv[k+1]:recv[k]+0.1;
      for(double t=recv[k]; t<nxt; t+=1/60.0){
        Vec3 a=e.at(t); double err=std::fabs(a.x-S*(t-1000)); if(k>=5 && err>maxerr) maxerr=err; }
    }
    printf("bunched then 25ms: max error %.1f units (%.2f s of flight)\n",maxerr,maxerr/S);
  }
  { // case 3: random jitter 0..40 ms, receive times quantized to 30 fps guest frames
    std::mt19937 rng(1); std::uniform_real_distribution<double> j(0,0.04);
    VelocityEstimate e; double maxerr=0, sum=0; int n=0; std::vector<double> rv;
    for(int k=0;k<2000;k++){ double r=1000+k*0.1+0.03+j(rng); rv.push_back(std::ceil(r*30)/30); }
    for(int k=1;k<2000;k++) if(rv[k]<rv[k-1]) rv[k]=rv[k-1];
    for(int k=0;k<2000;k++){
      e.update({(float)(S*k*0.1),0,0}, rv[k]);
      double nxt = k<1999? rv[k+1]:rv[k]+0.1;
      for(double t=rv[k]; t<nxt; t+=1/30.0){ Vec3 a=e.at(t); double err=std::fabs(a.x-S*(t-1000));
        if(k>5){ if(err>maxerr)maxerr=err; sum+=err; n++; } }
    }
    printf("jitter 0-40ms @30fps: max error %.1f units (%.2f s), mean %.1f\n",maxerr,maxerr/S,sum/n);
  }
}
```

Output on main 563f72d:

```
k=19 recv=1001.930 vel=100.0 err=0.0
k=20 recv=1002.105 vel=74.3 err=8.3
k=21 recv=1002.130 vel=269.7 err=5.1
k=22 recv=1002.230 vel=167.9 err=2.0
k=23 recv=1002.330 vel=127.2 err=0.8
k=24 recv=1002.430 vel=110.9 err=0.3
single 75ms-late packet: max error 22.1 units (speed 100 u/s => 0.22 s of flight)
bunched then 25ms: max error 40.5 units (0.41 s of flight)
jitter 0-40ms @30fps: max error 18.9 units (0.19 s), mean 3.6
```

In game: a lane with `tc netem` jitter (for example `delay 30ms 20ms`) and `play_civ.py` / the space sync test shows the max error outliers growing with the jitter.

### Expected
The velocity is computed over the sender's time between the two snapshots, so a late or bunched packet changes only when a snapshot is shown, not how fast the mirror moves. Under 40 ms jitter the error should stay at the level of the speed changes, a few units, not about 20 to 40.

## Root cause
**What happens**

`update(p, now)` computes `v = (p - pos) / (now - t)`, blends it into the velocity with weight 0.6 when 0.02 s < dt < 1 s, and `at(now)` returns `pos + vel * min(now - t + 0.03, 0.4)`.

With snapshots every 0.1 s, a ship at 100 u/s and a 30 ms path, one packet arriving 75 ms late gives:
- that packet: dt = 0.175 s, estimated speed 74 u/s;
- the next one: dt = 0.025 s, estimated speed 270 u/s, while the real speed is still 100 u/s.

The mirror is pushed ahead with 270 u/s until the next snapshot and needs four more snapshots to settle. Harness results at 100 u/s:

| case | max position error |
|---|---|
| one packet 75 ms late | 22.1 units (0.22 s of flight) |
| two packets in the same guest frame, the next 25 ms later | 40.5 units (0.41 s of flight) |
| random jitter 0..40 ms, guest at 30 fps | 18.9 units max, 3.6 mean |

With no jitter the error is 0, so the whole error comes from using the receive time.

This is a candidate cause of the civ vehicle outliers in docs/known-issues.md ("the maximum of ~2500 samples was 22-27 m in three sessions"): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/known-issues.md#L283-L289

### Confidence
Confirmed with the harness above (real `interp.h`). A second reviewer verified it. Severity low to medium: it is visible as mirrors jumping and rubber-banding on jittery links (Wi-Fi), and it adds to hit and position errors on fast ships.

## Fix
Fixed on fix/w2-net. The host appends its `net::now_ms()` (u32) after the last entry of every
`MSG_UFO_STATE` (squadron.cpp) and `MSG_CIV_STATE` (civ.cpp, chunk budget +4 bytes) chunk; the guest
finds it by the entry size (`net::snapshot_time`), so older guests ignore it and an older host's
chunks fall back to the arrival time. `VelocityEstimate::update(p, now, sender_s)` uses the host's
interval for the speed and ages the snapshot in host time mapped onto the local clock by a
`ClockOffset` (the BUG-0133 window), so a late snapshot is moved on further and a bunched one is
not. Switch: `coop.corefix sender_time 0`.

The review's suggestions were:

- Put the host's `net::now_ms()` (u32) in the header of the ship and vehicle snapshot chunks, next to the u16 cycle.
- Change `VelocityEstimate::update(p, now, sender_s)` to use `dt = sender_s - last_sender_s` for the speed. Keep the local receive time only for the age in `at()`, or better use the `SnapshotBuffer` offset logic so the age is in sender time too.
- For compatibility with older peers, fall back to `cycle_delta * 0.1` when no time is sent.

## Verification
Unit test `test_velocity_jitter` at 100 u/s with 0.1 s snapshots and a 30 ms path: one packet
75 ms late: max error 0.1 units with the fix, 22.1 with the arrival time (`--nofix`, FAIL);
packets 20 and 21 in one frame and 22 60 ms late: 0.1 vs 9.9 (FAIL). The ticket's second case put
packet 22 45 ms before its send time on the truth clock, which no network does; the test keeps
every packet at or above the 30 ms path. Not measured in game (play_civ.py outliers, BUG-0186).

Planned in the review:

New case `test_velocity_estimate_jitter` in `tests/net_tests.cpp`:
1. Feed the case-1 stream above (0.1 s snapshots at 100 u/s, packet 20 arriving 75 ms late).
2. `CHECK(max error < 5.0f)` over the frames after each snapshot.

On main it FAILS (22.1). With the sender-time `update` it PASSES (the error stays under 1 unit, at the level of the extrapolation over the late gap). Add the bunched case (main 40.5) as a second CHECK.

### Related
#15 (one lost ship-state packet makes ships explode and respawn, same snapshot stream), #31 (millisecond clock wrap in the same file), docs/known-issues.md civ vehicle outliers (linked above), tracking issue #82.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #101.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-net (wave 2, netcode); tests above FAIL without the fix and PASS with it.
- 2026-10-28: released in 0.7.3.0.
