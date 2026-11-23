---
id: BUG-1400
title: cell_attack_anim: the idle partner's own plant meal (its eat animation 0xB) counted as an attack animation
status: fixed
kind: test
stage: Cell
reported: 2026-11-21
source: lab rel/0.7.16 20261121-003202 cell_attack_anim
role: 
versions: unreleased
fixed_in: 0.7.16
branch: fix/w62-flake-cc
commits: e4076da2
tests: cell_attack_anim, cell_attack_jaw, cell_attack_spike, cell_attack_mouth
signature:
docs: docs/re/cell.md#cell-eat-anim
related: BUG-1243
---

## Report
0.7.16 third final check (lab 20261121-003202, lane 6): cell_attack_anim FAIL "the guest's own cell, idle, plays no new
animation while the host attacks (['B'])". Known flake (3 of 7 on rel/main); the earlier FAIL runs (fix/w53-cellfx
20261118-124151 / -125159: "the bites land ... 0 of 5") had another cause (BUG-1355 era, no creature on the screen).

## Root cause
Observed: the guest's log: `eat: own cell ate +6 plant (total 6)` at s 26.65 (and the host applied the guest's meal at s
26.66); the guest's effects log starts at s 15.9 (`coop.effects log on` of the test), so the host's bite phase ran from
about s 18.6 to s ~33 and the guest's own bites start at s 35.9 (`bite: sent`). The guest (a herbivore, diet=herb) sat
idle; a plant reached its mouth and its own game ate it (AvatarEat, `own_eats` +1) inside the host's phase. Its
+0x1B0 showed 0xB then. In lab 20261119-102828 (PASS) the host's own avatar showed 0xB with its own plant meal at s 23.8.
Inferred: 0xB is the cell's eat (plant) animation; the idle partner played its own meal, not the attacker's bite. The
host's avatar played only 0 during its phase (the fixture's bite plays no attack animation, docs/re/cell.md
#cell-partner-attack-anim), so nothing of the host's attack reached the guest's cell. A test fault, old: not new in
0.7.16 and not from a w61 change.

## Fix
`coop.pool anims` prints the own cell's `own_eats` (the mod's count of the game's own AvatarEat). The test's samplers
note each rise; the idle cell's samples from 1 s before to 3 s after an own meal are kept apart and printed (its own
meal), the rest is judged as before. The contact variants judge the other cell's own animations the same way; the
puppet check still uses all of them. Not a loosened check: an attack animation of the partner outside the idle cell's
own meals still fails, and BUG-1243's twin cases stay the same.

## Verification
no-fail-run: test-only change (the meal of an idle cell cannot be forced). Lab: `SPORE_DETECTOR=20 python3 tools/lab.py
run cell_attack_anim,cell_attack_jaw,cell_attack_mouth --lane N --retry 0 --no-build` PASS (`make -j8` first: the dll prints
own_eats). Was waiting for the lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): cell_attack_anim, cell_attack_jaw and cell_attack_mouth PASS in 20261122-194210 and 20261123-023643. Closed.

## History
- 2026-11-21: opened from the third final check of 0.7.16 (fix/w62-flake-cc); fixed in the test.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
