---
id: BUG-0481
title: play_tribe fails on every run: tribe members not where the host has them (sync t*: 90% within 1.5 m), NPC proxies over 3 m off, the minimap view
status: fixed
kind: test
stage: Tribe
reported: 2026-11-03
source: nightly detector run 2026-11-03
role: guest
versions: 
fixed_in: 0.7.4
branch: fix/w40-play
commits: e8c36083
tests: play_tribe
signature:
docs: docs/re/tribe.md#tribe-mirror-lag
related: BUG-0365, BUG-0503
---

## Report
play_tribe: 0 OK of 9 runs (6 before 2026-11-03, 3 in out/lab/20261103-012624). Failing checks: `sync t*: 90% within 1.5 m` (3/3), the proxies monitor rule (1 NPC proxy over 3 m off the host's NPC), `guest: the minimap takes the view where it points`. Either the Tribe member sync is worse than the limit under real input or the limit / measurement is wrong: measure which (what the guest sees) before changing the limit.
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (measured what the guest's game shows: play_tribe's probe reads the host's coop.sync back to back for
0.6 s before each guest read and compares the guest's member with every point of that path; lab
20261103-150636, 298 samples): standing members raw p90 0.04 m; walking members (p50 8.4 m/s, they run to
their orders) raw p90 2.14 m to where the host has them now, but on the host's path 0.21 / 0.22 / 0.25 s back
(best lag p10 / p50 / p90, max 0.37 s) and 0.09 m p90 (max 0.70 m) off it there. The lag is the mirrors'
interpolation delay kMirrorDelay = 0.2 s (BUG-0365's buffer) + ~0.02 s; 0.22 s x 8.4 m/s = 1.85 m = the
failing p90 (1.78-1.89 m on main). No mirror lags beyond that, walks its own path or is snapped: a measurement
error. play_tribe compared with the host's present position, while the 1.5 m limit (coop_sync.py LIMITS) is
for coop_sync's measure against the host's path over its lag window.
The other two failure kinds: the NPC proxies monitor rule (an animal proxy 3.5-8 m off, 1 of 3 runs) is a
separate desync, not analysed: BUG-0503. The guest's minimap calibration (1 of 3, 20261103-012624 #3) was not
seen again; play_tribe now prints the three calibration points and the busy state when it fails.
## Fix
Test only (the limit unchanged): play_tribe compares a citizen with the host's path over TRIBE_LAG = 0.32 s
(the 0.2 s delay + one 0.12 s send interval of the host), as coop_sync.py does with LAG_WINDOW, and prints the
raw p90 and the probe (standing / walking, lag windows, best lag, rows in out/lane<N>/track/). A planted late
mirror for the check: `coop.tribe delay <s>` on the guest (tribe.cpp, tests), `PLAY_TRIBE_GUEST_CMDS`.

## Verification
- the measurement: out/lab/20261103-150636 (old check FAIL: raw p90 1.89 m; on the path 0.09 m p90);
- the new check still catches a late mirror: PLAY_TRIBE_GUEST_CMDS="coop.tribe delay 0.6", FAIL
  out/lab/20261103-152720 (`sync t*: 90% within 1.5 m of the host's path over 0.32 s`: p90 2.71 m, best lag
  p50 0.60 s);
- PASS with the normal delay: out/lab/20261103-160342 play_tribe#1-#3 (`sync t*` ok 3 of 3: p90 0.23 / 0.07 /
  0.09 m on the path, raw 1.73 / 1.93 / 1.32 m; walking best lag p50 0.22 s). play_tribe passed 1 of 3 (0 of 9
  before); #1 and #3 failed on the orders (`gave orders through real clicks` 8 / 9, guest members idle on 12 /
  7 orders): BUG-0505.

## History
- 2026-11-03: opened.
- 2026-11-03: fixed on fix/w40-play (a measurement error: the check compares with the host's path over the mirrors' delay); the proxies part split into BUG-0503.
- 2026-11-03: released in 0.7.4.
