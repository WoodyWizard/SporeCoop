---
id: BUG-0787
title: Civ: a guest's vehicle mirror stands and teleports when the host's vehicle is more than twice as fast as the mirror (jump limit from the mirror's own speed)
status: open
kind: desync
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
