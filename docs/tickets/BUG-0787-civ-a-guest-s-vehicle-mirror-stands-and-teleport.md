---
id: BUG-0787
title: Civ: a guest's vehicle mirror stands and teleports when the host's vehicle is more than twice as fast as the mirror (jump limit from the mirror's own speed)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-civ-speed
commits: 69eecd6d, 942fa887, 1342d697
tests: civ_fast_mirror
signature: guest copy of a fast host vehicle trails 5-7 m, coop.civ jumps= grows fast
docs: docs/re/civ.md#civ-boost-stacking, docs/re/civ.md#civ-boost-measured, docs/06-testing.md#testing-civ-speed
related:
---

## Report
Guests see the host's vehicles stop, then jump ahead, again and again, when the host's vehicles move fast (for example after the alarm speed boost of GitHub #199 stacked). Found by reading the code, not reported on its own.

## Root cause
Observed in the code, confidence medium. civ.cpp:3772 (vehicle mirror driver): `max_speed = 2*F<float>(v,0x218)+10`, where `v` is the guest's mirror, whose own max speed (cVehicle +0x218) is not boosted. A host vehicle moving faster than that makes every snapshot step a 'jump': interp.h:838-839 zeroes the velocity, the mirror stands, and the glide only catches up when d < 30 m (civ.cpp ~3913), otherwise it teleports. Inferred: this adds the 'flicker' to the 'moves like flash' report of GitHub #199. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Send the host vehicle's +0x218 (or its measured speed) in the vehicle snapshot and compute the jump limit from it, not from the mirror's own field.

## How to test
Two instances in Civ; on the host write a vehicle's +0x218 to 4x its value (mem.write, or the alarm boost) and drive it; on the guest log the mirror's velocity and position per frame: FAIL = velocity reset to 0 and position jumps > 30 m repeatedly; PASS = the mirror follows smoothly at the host's speed.

## Fix
The host sends each vehicle's top speed (+0x218, boost included) after the snapshot time in MSG_CIV_STATE (a tail
of n x f32); the guest's jump limit is `2 x max(own top, host top) + 10`. A 0.7.12 guest of a 0.7.13 host only loses
the sender time (velocity from arrival times); a 0.7.13 guest of an older host uses its own top as before. The guest's
`coop.civ` vehicle line shows `htop=`. Switch `coop.fix civ.vel_cap_host off` = the copy's own top speed.
Correction of the ticket's guess: the copy does not stand and teleport; with the velocity zeroed at every snapshot it
is only glided half way per frame and trails 5-7 m behind at 55 m/s.

## Verification
Test `civ_fast_mirror` (tools/tests/coop_civ_fast_mirror.py): the host's vehicle at 55 m/s (copy's own top 19.1,
limit 48), driven for 20 s, `coop.bodyrec` on both games, the copy against the host's vehicle at the same moment.
* FAIL: out/lab/20261112-003833 and out/lab/20261112-003654 (`SPORE_FIX_OFF=civ.vel_cap_host`, lane 3): p50 4.9 m,
  p90 6.8 m, `jumps=130`.
* PASS: out/lab/20261112-003838 and out/lab/20261112-003659 (lane 2): p50 0.7-0.9 m, p90 1.8-2.3 m, `jumps=2`.

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: fixed on fix/w48-civ-speed (civ.vel_cap_host), FAIL 20261112-003833, PASS 20261112-003838.
