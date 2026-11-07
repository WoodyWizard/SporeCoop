---
id: BUG-0651
title: Tribe: under a loaded guest the member lead goes off on any link (the one-way latency the drive.lead_smooth rule reads rises to 110-150 ms) and members trail as in 0.7.8
status: open
kind: desync
stage: Tribe
reported: 2026-11-06
source: fix/w45-lead-player lab runs (BUG-0650)
role: guest
versions: unreleased (rel/0.7.9 + drive.lead_smooth)
fixed_in:
branch:
commits:
tests: body_trail_tribe, body_trail_tribe_player
signature:
docs: docs/re/tribe.md#tribe-lead-late
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

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
