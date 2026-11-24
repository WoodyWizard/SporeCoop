---
id: BUG-1466
title: nest_eggs_air: a freshly laid egg read 0.48-0.52 m over the surface before the grounding placed it (3 runs)
status: fixed
kind: test
stage: Creature
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in: 0.7.17
branch: fix/w84-tests
commits: c31b2232
tests: tools/tests/coop_nest_eggs_air.py (nest_eggs_air)
signature:
docs:
related: BUG-0384
---

## Report
Validation run of 0.7.16 (lab 20261123-023643, mod code 5376d388): nest_eggs_air "FAIL the eggs the game laid lie on the surface (over 0.3 m: {'1E1FD4E0': 0.52})": `host eggs after 38 s (ptr: over surface): {'1416B1C0': 0.04, '1E1FD4E0': 0.52}`; later `host eggs now (over surface, m apart): [0.0, 0.0]`, `nest eggs fix=1 grounded=2`. The same signature twice before (0.48, 0.49).

## Root cause
Test timing (observed, lab 20261123-023643 nest_eggs_air): the check read the laid eggs once, 6 s after the laying
loop ended: egg 4627 (ptr 1E1FD4E0) 0.52 m up; the host's game log has `nest: egg 4627 hung 0.48 m over the surface: put
onto it (BUG-0443)` at s 60.66, 6.5 s after that read (the lifted egg 4626 was grounded at s 56.13). The host's grounding
(nest.cpp ground_tick, every 0.5 s within 60 m of the host) puts an NPC egg down only after it stood still (moved under
2 cm) for two checks: egg 4627 was still moving (awake, settling on the mound) for over 12 s after its laying.
Inferred: the game's own physics keeps a fresh egg awake that long now and then; nothing wrong in the grounding. A
player could see such an egg 0.5 m up while it slowly settles, for ~10-15 s. Not a mod bug as far as these rows show.

## Fix
coop_nest_eggs_air.py: after the laying the eggs are read every 0.5 s for at least 6 s (as before) and up to SETTLE_S
= 30 s, until every egg lies under HANG (0.3 m); each egg's trace (s, height over the surface, move since the last read)
and the host's `grounded=` count are printed; an egg still over 0.3 m after 30 s fails ("within 30 s of the laying").
The lift part is unchanged. Test only, no mod change.

## Verification
Open, pending night run: `SPORE_DETECTOR=20 python3 tools/lab.py run nest_eggs_air --repeat 4 --lane N --retry 0 --no-build` PASS 4 of 4; the per-egg trace shows when each egg
landed (by the game or by the grounding). FAIL before: 20261123-023643 (rel/0.7.16 fd28f88f).
0.7.17 release lab: PASS: nest_eggs_air (lab 20261124-020506); no-fail-run: verified by 0.7.17 release gate lab run.

## History
- 2026-11-23: opened (fix/w65-tests).
- 2026-11-23: fix/w84-tests (WP-T1 of 0.7.17): the laid eggs are read every 0.5 s up to 30 s before they are judged (c31b2232).
- 2026-11-24: released in 0.7.17.
