---
id: BUG-0612
title: class: the rest of BUG-0425 that is not the placing or timing of a remote body (one CycleTracker, NaN guards, oversize messages, Space ship caps, root motion, hops)
status: fixed
kind: bug
stage: All
reported: 2026-11-06
source: BUG-0425 umbrella (split by fix/w45-remote-bodies)
role: 
versions: 
fixed_in: unreleased
branch: fix/w45-remote-bodies-2
commits: db4b6090, 33ae8911
tests: validate_tests, net_tests, net_nan
signature:
docs:
related: BUG-0425, BUG-0614, BUG-0615, BUG-0616, BUG-0190, BUG-0388, BUG-0414, BUG-0376
---

## Report
Split from the umbrella BUG-0425 when its placing / timing part was fixed (fix/w45-remote-bodies, drive.lead). Its
other parts, from docs/generalisation-plan.md G8, are not about where a remote body is drawn and were not touched:
Civ / Tribe accept a late chunk of an OLDER cycle (civ.cpp, tribe.cpp: objects recreated, one CycleTracker wanted);
no NaN / Inf guard on received poses outside Cell (game.cpp game::teleport / set_position / place); oversize
unreliable messages dropped silently (connection.cpp); Space ship mirrors without a speed cap / extrapolation
limit (squadron.cpp defaults, BUG-0190, BUG-0388); root motion of abilities (BUG-0414); hops (BUG-0376).

## Root cause
An umbrella, split into concrete parts (2026-11-06, fix/w45-remote-bodies-2):
* One CycleTracker / a late chunk of an older cycle (Tribe, Civ): BUG-0616, fixed (net.cycle_order).
* NaN / Inf guard on received poses: BUG-0614, fixed (game.finite_pose; the lab showed no visible failure without it on the player path).
* Oversize unreliable messages dropped silently: BUG-0615, fixed (counted and logged).
* Space ship mirrors without a speed cap / extrapolation limit: no change. squadron.cpp's `net::VelocityEstimate` has `max_ahead` 0.4 s (an extrapolation limit) and the 200-unit jump check; a speed cap would cut real fast ships (~200 m/s core orbits, BUG-0388, fixed in 0.7.6, drawn within 3 m of its path in 98 % of the fast rows); BUG-0190 (the ships' p90 near its limit) is not reproduced. Nothing observed to fix.
* Root motion of abilities: BUG-0414, fixed in 0.7.3.4 (the 'pops' were out-of-view grounding steps, BUG-0434).
* Hops: BUG-0376, fixed in 0.7.5.

## Fix
See the parts: BUG-0614 (game.finite_pose), BUG-0615 (net: counted and logged), BUG-0616 (net.cycle_order).

## Verification
no-fail-run: an umbrella; each part has its own verification (BUG-0616: validate_tests --nofix FAILs, PASS without; BUG-0615: net_tests; BUG-0614: net_nan 20261106-042950 / 20261106-043542).

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-remote-bodies-2, M2): split into BUG-0614, BUG-0615, BUG-0616 (fixed) and the existing BUG-0190 / BUG-0388 / BUG-0414 / BUG-0376 (closed before); closed as the umbrella.
