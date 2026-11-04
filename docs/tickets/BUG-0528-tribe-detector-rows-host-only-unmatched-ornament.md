---
id: BUG-0528
title: Tribe: detector rows host_only / unmatched ornament (noun 0x018C88E4) in play_tribe: one ornament on the host the guest has not
status: open
kind: desync
stage: Tribe
reported: 2026-11-04
source: fix/w41-tribe-orders, detector rows in play_tribe (lab 20261104-010027, 20261104-000550, w40-play 20261103-160342)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
The divergence detector (SPORE_DETECTOR=20) in play_tribe prints, in 4 runs of 2026-11-03/04 (out/lab/20261103-160342
play_tribe#2 of fix/w40-play, out/lab/20261104-000550, 20261104-010027 #1 and #2 of fix/w41-tribe-actions):
`Tribe:unmatched:ornament` "ornament: 3 on the host, 2 on the guest, 1 host / 0 guest without a partner within 3 m"
(from ~160 s, during the hunt phase) and `Tribe:host_only:ornament` "1 ornament near the guest on the host, none on the
guest (e.g. at (326 -44 376))" (~340 s, gather phase). `ornament` is noun 0x018C88E4 in worlddiff.py (not the
interactive ornaments 0x03A2511E of BUG-0459 / BUG-0473, which the detector maps to BUG-0488 in Tribe).

## Root cause
Not analysed (fix/w41-tribe-orders ran out of time). Observed: always exactly one ornament more on the host, first seen
in the hunt phase, already before the 0.7.5 Tribe merges (w40-play). Inferred, not checked: an object the host's game
leaves at a hunt (a carcass / meat ornament of a killed animal, the GatherMeat target) or a decoration the host's tribe
gets (the totem pole, cTribe +0x19D0) that the guest's game does not make, since the animals are proxies on the guest
and the kill runs on the host. Real for a player if it is the meat: the guest's members could not be ordered to
gather it (no object to click). Next: `coop.tribe objs 18C88E4` on both games after a hunt kill.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
