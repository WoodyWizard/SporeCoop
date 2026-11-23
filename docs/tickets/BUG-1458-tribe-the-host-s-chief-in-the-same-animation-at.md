---
id: BUG-1458
title: Tribe: the host's chief in the same animation at the same moment stands another way on the guest, the staff on the other side (BUG-1072 seen again)
status: open
kind: desync
stage: Tribe
reported: 2026-11-23
source: lab
role: guest
versions: 0.7.16
fixed_in:
branch:
commits:
tests: tribe_mirror_stale (chief_t3), tribe_chief_tracks (chief2)
signature:
docs:
related: BUG-1072, BUG-0988, BUG-1069, BUG-1347
---

## Report
Key frame review of the 0.7.16 final check, lab 20261122-194210, the same camera on both games at the host's chief:
* `out/lab/20261122-194210/frames/sheets/072_tribe_mirror_stale_03_chief_t3.jpg`: both games report the chief in the same animation at the same moment
  (`host anim=04C6CB02 bhv=04C6CB02 hold=1 t=2.458; guest anim=04C6CB02 bhv=04C6CB02 hold=1 t=2.442`), but the host's
  chief stands bent forward with the staff raised to its left, the guest's copy upright with the staff to its right.
* `out/lab/20261122-194210/frames/sheets/057_tribe_chief_tracks_02_chief2.jpg`: the host's chief holds the staff on the left of the picture with its head
  turned away, the guest's copy on the right with its head to the side.
* Smaller differences of the same kind: tribe_mirror_hold 01 / 02 (swing / hold), tribe_own_outfitter 03, and in older
  runs tribe_chief_tracks 01 / 02 of lab 20261120-133703 and 20261121-003202.
This is BUG-1072's picture (fixed in 0.7.14 by tribe.mirror_phase_tracks); the test tribe_chief_tracks passes on its
track timing (183 of 190 samples within 0.3 s) while the frames still differ.

## Root cause
Not analysed. The phase of the tracks is right in the samples; the difference may be the heading of the copy, the
hand that holds the staff, or a blend / layer the track sync does not carry.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened (fix/w66-frames, key frame review of the 0.7.16 final check, lab 20261122-194210 on 5376d388).
