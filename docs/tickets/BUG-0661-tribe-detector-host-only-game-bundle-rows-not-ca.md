---
id: BUG-0661
title: Tribe: detector host_only game_bundle rows (not carried bundles) 2-5 rows a run in play_tribe, not covered by BUG-0617
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-06
source: lab log sweep follow-up 2026-11-06 (detector rows rel/0.7.8 vs rel/0.7.9)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in: unreleased
branch: fix/w46-k8-open
commits: 904d33e0
tests: host_bundle_drop, host_bundle, host_bundle_carry, play_tribe
signature:
docs: docs/re/tribe.md#tribe-bundle-reclass
related: BUG-0548, BUG-0617, BUG-0684
---

## Report
play_tribe Tribe:host_only:game_bundle rel/0.7.8 1-2 rows of n2, rel/0.7.9 2-5 rows of n2-3. BUG-0617 covers carried bundles; BUG-0472 notes plain bundles have no kind yet.

## Root cause
Observed: in play_tribe (fix/w46-k2a-state lab 20261107-232019) a `host_only:game_bundle` at (335 -156 345) stood for 2
samples next to a `guest_only:game_bundle_carried` at (320 -159 363) of a guest member; the guest had made the host's
carried bundle at (337 -157 343) earlier. Reproduced (host_bundle_drop, fix off, lab 20261108-010006, 2 of 2): a host
member carrying food dies, its game moves the SAME bundle object (same pointer and id) into its ground container
(class carried -> ground); the guest's copy stayed bound (worldobj binds by id once bound, the class is checked only
at the bind) and stayed in the hand of the member's copy: the host's ground food was missing on the guest, and the
copy was carried on. The other way (a member takes ground food) is the same case.

## Fix
`wobj.bundle_reclass` (worldobj.cpp, Kind::reclass): a bound copy whose host object's class differs from its own is
removed (the kind's apply kGone: RemoveBundles of the copy), the bind dropped, and the host object handled as unbound in
the same snapshot (bound to a copy of the right class within tol, else made in the right container). `wobj` lists
`reclassed=`. Off: 0.7.10.

## Verification
host_bundle_drop (`coop_world_host.py --case bundle_drop`: a member of the host's tribe carries food, `coop.wobj
bundle carry`; it is killed, `coop.tribe pincer`; 6 s later every host ground bundle there has a guest ground bundle
within 3 m and no guest carried copy lacks a host one), lane 5:
- FAIL with SPORE_FIX_OFF=wobj.bundle_reclass: out/lab/20261108-010006 (2 of 2 drops: guest ground 0, carried 1).
- PASS: out/lab/20261108-011630-2 (x2, with host_bundle x2 and host_bundle_carry x2).
- Detector: see the History line of the final play_tribe run.

## History
- 2026-11-06: opened from the lab log sweep follow-up; planned for 0.7.10.
- 2026-11-07 (orchestrator): also takes the organic play_tribe Tribe:unmatched:game_bundle rows that BUG-0617's fix does not change (on 9/30 lines, off 12/5: out/lab/20261107-021641-2, -023842).
- 2026-11-08: fixed on fix/w46-k8-open (wobj.bundle_reclass); coordinated with K7 (fix/w46-k7-findings, BUG-0684 worldobj bundles: no commits on its branch at the time).
