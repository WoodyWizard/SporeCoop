---
id: BUG-0677
title: Space: a guest does not see the beam shots of the ships the host's game runs (no default_beam_projectile in a guest's game)
status: open
kind: desync
stage: Space
reported: 2026-11-07
source: lab log sweep BUG-0657 (detector Space:noun_only_host / host_only:default_beam_projectile)
role: 
versions: 0.7.8, 0.7.9, rel/0.7.10
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0477, BUG-0657
---

## Report
Detector (lab log sweep, BUG-0657): `Space:noun_only_host:default_beam_projectile` (16 runs) and `Space:host_only:default_beam_projectile`
(12 runs, e.g. play_space, campaign_space; "1 default_beam_projectile near the guest on the host, none on the guest"). Unlike the Civ
shots this class persists: the next sample still had it in 11 of 14 / 8 of 11 cases, and no run ever had a beam in the guest's game
only or unmatched (the guest's game has none at all). How to see it: play_space with SPORE_DETECTOR=20, then
`tools/detector_report.py <run>`.

## Root cause
Inferred from the code (not run): squadron.cpp h_FireTool returns 0 for every mirror ship in a guest's game ("mirrors never fire on
their own"), the hits come from the host (MSG_UFO_HIT); civ::fire_observed relays the host's FireTool calls only for Civ vehicles and
turrets (BUG-0477: "The Space half (`default_beam_projectile`) is not covered here"). So the guest sees the host's ships and the NPC
ships the host runs fight without their beams. Not fixed in 0.7.10 S1 (a relay of Space shots like civ.shots is a feature-size
change; found by the S1 sweep, outside its six tickets).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
