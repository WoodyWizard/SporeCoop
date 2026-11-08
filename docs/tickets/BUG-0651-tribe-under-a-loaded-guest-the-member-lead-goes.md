---
id: BUG-0651
title: Tribe: under a loaded guest the member lead goes off on any link (the one-way latency the drive.lead_smooth rule reads rises to 110-150 ms) and members trail as in 0.7.8
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-06
source: fix/w45-lead-player lab runs (BUG-0650)
role: guest
versions: unreleased (rel/0.7.9 + drive.lead_smooth)
fixed_in: 0.7.11
branch: fix/w46-k4-bodies
commits: a25b8569, 1005b829, afc003d4
tests: body_trail_tribe_player, tribe_walk_mobile, net_tests test_lead_fall (lead_off_with)
signature:
docs: docs/re/tribe.md#tribe-lead-fall, docs/06-testing.md#testing-lead-fall
related: BUG-0619, BUG-0650, BUG-0410, BUG-0425
---

## Report
Found by the BUG-0650 agent (fix/w45-lead-player) at a lab load of 11-19 on 6 cores (a release batch on the other
lanes): body_trail_tribe (a local link, no netem) and body_trail_tribe_player failed for the members on M1 + M3's code
(SPORE_FIX_OFF=drive.lead_late) and with BUG-0650's fix alike: members p90 2.41-3.22 m, lag p50 0.40-0.70 s, p90
0.81-1.15 s (limits 1.5 / 2.0 m); out/lab/20261106-210300 (fix), -210310 and -205423 (M1). M3 measured 1.05-1.37 m at
load 4-9. The guest logs show `tribe: mirror #N lead off (one-way latency 110-150 ms, BUG-0619)` for every member early
in each run, also on the local link; in tribe_walk_player runs the lead came back on (75-84 ms) after 10-15 s.

How to reproduce: tools/lab.py run body_trail_tribe while other lanes load the machine (load over ~10 on 6 cores);
grep 'lead off' in the guest's game log.

## Root cause
Observed: drive.lead_smooth (BUG-0619) turns the member lead off over a one-way latency over 100 ms (the session's half
round trip, median of 64 states); under load that latency is 110-150 ms on a local link. Inferred: the round trip is
measured in game frames on both ends, so a loaded or slow guest (or host) adds its frame times; the rule then reads a
slow machine as a jittery network and shows members as 0.7.8 does (interpolated, 0.2 s + latency late). Not worse
than 0.7.8, but the BUG-0410 / BUG-0425 gain is lost on a loaded PC. A measure that parts a jittery link from a slow
machine (the states' arrival jitter did not at the lab's load, docs/re/tribe.md#tribe-lead-smooth) is needed.

K4 (observed, guest log lines `net: round trip p10 .. p90`, added): the mean round trip the rule read
(Connection::rtt_ms, ~10 samples) follows every spike of the player network and both games' frames: 115-462 ms at a lab
load of 25-35; the rule read 139 ms and the members were shown interpolated for the rest of the run (body_trail_tribe_player
p90 2.11 m, lab 20261108-003907-2). The lower quartile of the last 128 samples leaves the spikes out: the player network
84-151 ms, the mobile network 186-234 ms.

## Fix
`coop.fix drive.lead_low`: the no-lead rule reads g_one_way_low, half the lower quartile of the last 128 round-trip
samples (Connection::rtt_quantile), each without half this game's own wait between two socket reads (poll_prev_ms: a loaded
guest's frame), lines 80 / 70 ms. The mobile network still goes off (tribe_walk_mobile `lead off (one-way latency 101 ms)`),
the player network at a lab load of 25-35 reads 80-152 ms and keeps the lead. Off: 0.7.10 (the mean, 130 / 115 ms).

## Verification
- FAIL: out/lab/20261108-003907-2 (the code before: `lead off (one-way latency 139 ms)`, members p90 2.11 m), out/lab/20261108-010745 #1
  (SPORE_FIX_OFF=drive.lead_low,...: lead off on 20 lines, p90 2.33 m); `SPORE_FIX_OFF=drive.lead_low build/host/net_tests`: FAILED (1).
- PASS: out/lab/20261108-005113 #1 (no lead off, members p90 1.83 m, OK), out/lab/20261108-010745-2 #2 (OK, no lead off at a load of
  ~30); tribe_walk_mobile out/lab/20261108-005832-2 OK (the mobile network still turns the lead off).
- Fragile: the player network's top (76 ms) is near the 80 ms line; body_trail_tribe_player also fails at a lab load of 30-35 for
  other reasons (both switch sides, docs/06-testing.md#testing-lead-fall).

## History
- 2026-11-06: opened.
- 2026-11-07 (fix/w46-k3-bodies, K3, BUG-0410): observed on the player network too (80-101 ms, the lead off for 25-60 s of a run). drive.lead_keep moves the rule's line to 130 / 115 ms: the player network and a moderate load keep the lead (body_trail_tribe_player 3 of 3 against 0 of 3, lab 20261107-114838 / -114838-2). Not fixed: a guest under a heavy load still reads 130-150 ms like the mobile network (109-126 ms at its switch); the two are not parted by this latency (the states' arrival lateness did not part them either, see above). A measure of the jitter that leaves the guest's own frame times out is still needed; open.
- 2026-11-08 (fix/w46-k4-bodies, K4): fixed (drive.lead_low: the round trip's lower quartile, without the own frame wait).
- 2026-11-08 (fix/w46-k4-bodies, 456ced2c, rel/0.7.11 merged in): final runs: out/lab/20261108-020506 (puppet_stops, body_trail_tribe, body_trail_tribe_player, body_trail_creature, body_trail_civ, body_trail_player OK; play_creature failed 'joint hunts' once, OK on rerun out/lab/20261108-023606), out/lab/20261108-020506-2 (play_civ, body_trail_tribe_hold, body_trail_civ_loss, body_trail_hits OK; play_tribe failed its order checks once (BUG-0397 / BUG-0531 class), OK on rerun out/lab/20261108-023535; puppet_stops failed only 'enough stops to judge' (6 of 8), 0 stops past), out/lab/20261108-023036 (puppet_stops 2 of 2): puppet_stops 4 runs, 0 of 53 stops over 0.3 m past.
- 2026-11-08: released in 0.7.11.
