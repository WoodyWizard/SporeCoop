---
id: BUG-0549
title: Civ: a guest's mirror vehicle drawn at scale 10 (the host's 0.81) in play_civ
status: fixed
kind: desync
stage: Civ
reported: 2026-11-04
source: divergence detector, play_civ (fix/w42-civ-left, out/lab/20261104-131223)
role: guest
versions: 
fixed_in: 0.7.9
branch: fix/w45-placement
commits: 9b993da9, a42f6e5f
tests: civ_vehicle_scale (coop_civ_vehicle_scale.py)
signature:
docs: docs/re/civ.md#civ-vehicle-scale
related: BUG-0485, BUG-0478
---

## Report
Detector, play_civ (fix/w42-civ-left, out/lab/20261104-131223): `Civilization:scale:vehicle` in 4 of 52 Civ comparisons:
one vehicle with scale 0.81 on the host and 10.00 on the guest. Reproduce: `SPORE_DETECTOR=20 tools/lab.py run play_civ`.

## Root cause
Observed (runtime log + Ghidra / objdump, 2026-11-06, fix/w45-placement): in the run of the report (fix/w42-civ-left
lab 20261104-131223) the SetScale diagnostic logged `scale 10 set on an object with vtable 01474BE8 from 00CA41DA` on the
guest only, at s 172. 0xCA41DA is in 0xCA40B0 (Vehicle_GroundSetup), called only from the vehicle's model message handler
0xCA76B0 (message 0x2B978C46): when the vehicle finds its ground object (+0xD00 from the manager 0xB34050) it sets its
loco's scale to 10.0. The host's vehicles never took that path (no such line in a host log of the November labs).
Inferred (not traced): which object +0xD00 is and why only a guest's mirror finds it. Not a spice claim (the ticket's guess).

## Fix
`civ.vehicle_scale` (civ.cpp): Civ vehicles are a replicated-fields kind (`fields::K_CIV_VEHICLE` = 8, field scale):
the host sends its vehicles' scale by net id, a guest's mirror that differs takes the host's, again whenever the game
sets its own. `coop.fix civ.vehicle_scale off`: the guest's own scale (0.7.8).

## Verification
civ_vehicle_scale (civ_npc; the guest's game gives a mirror the scale 10 with `coop.civ vscale`, the state the game's
handler leaves; the game's GetScale of that vehicle on both games for 15 s): FAIL with `SPORE_FIX_OFF=civ.vehicle_scale`
out/lab/20261106-061822 (10.0 vs the host's 0.75 for 15 s), PASS out/lab/20261106-062301 (the host's 0.75 within 1 s).
Not seen again in the play_civ detector run of this branch's baseline (out/lab/20261106-060553: no scale:vehicle row).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-civ-left): opened.
- 2026-11-06 (fix/w45-placement): the setter found (0xCA41DA, the vehicle's model-ready handler on guests), fixed with civ.vehicle_scale; runs above.
- 2026-11-06 (fix/w45-placement): regression with SPORE_DETECTOR=20: smoke out/lab/20261106-074134 (14/14), play_tribe out/lab/20261106-073013, play_civ / play_space and the touched tests out/lab/20261106-080336 (9/9; play_civ: no planet:building or scale:vehicle row, the guest's buildings_fixed=+0/-8 off_slot=0).
- 2026-11-07: released in 0.7.9.
