---
id: BUG-0526
title: Civ: a city's citizens are in one game only when only that game's camera is near it (guest_only / host_only:citizen with up to 19)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-04
source: play_civ --repeat 2 with SPORE_DETECTOR=20 (out/lab/20261104-005314), fix/w41-civ-citizens
role: 
versions: 
fixed_in: 0.7.6
branch: fix/w42-civ-left
commits: 38bc7b3f 7fdfa287
tests: coop_civ_citizen_lod.py (civ_citizen_lod)
signature:
docs: docs/re/civ.md#civ-tribe-raids
related: BUG-0476
---

## Report
After BUG-0476 (citizens near a guest follow the host's), play_civ with the detector (2 runs, 62 Civ comparisons, out/lab/20261104-005314) still has `Civilization:guest_only:citizen` in 19 % (e.g. 15 citizens on the guest, none on the host at a city) and `host_only:citizen` in 5 % (19 on the host, none on the guest), and `unmatched:citizen` in 39 % (247 objects, about 4 per comparison; before the fix 59 % and about 17). The quiet detect_civ (both cameras at the capital): unmatched 24 %, 20 objects. Reproduce: `SPORE_DETECTOR=20 tools/lab.py run play_civ`.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (coop_civ_citizen_lod.py, out/lab/20261104-130500): not a desync. Each game has a pool of 150 citizens per
planet and puts 19 of them at the city nearest its own camera: both cameras at the capital: 19 / 19 there; the guest's
camera at an NPC city 855 m away: the guest's game moved its 19 there (0 at the capital), the host's kept its 19 at
the capital; back at the capital 19 / 19 again; the totals stayed 150 / 150. The same level of detail as the Space
stage's colony citizens (docs/re/space.md#space-planet-life).

## Fix
Detector only (tools/tests/worlddiff.py): Civilization citizen rows are skipped where one game has no citizens near
the guest (a city only one camera is at); where both have them they are compared (worldobj `citizen` binds and walks
them). No game change, no switch.

## Verification
no-fail-run: level of detail, not a desync; coop_civ_citizen_lod.py shows the pool (PASS: out/lab/20261104-130500).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-civ-left): level of detail (the citizen pool follows each camera); the detector skips those rows.
- 2026-11-04: released in 0.7.6.
