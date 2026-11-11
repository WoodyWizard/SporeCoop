---
id: BUG-0883
title: Space: terraforming done while no partner is on the planet never reaches the other games (deltas dropped)
status: open
kind: desync
stage: Space
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0749, BUG-0766, BUG-0763
---

## Report
A player who terraforms a planet while no partner is on that planet changes it only in their own game (example: a guest raises a colony planet's T-score alone). The host's planet record (the host's world, so every later share) keeps the old atmosphere and temperature; when the partner lands later, the two games show the same planet at different T-scores for good.

Severity: medium. Confidence: high.

## Root cause
* Observed (code): mods/coop/spacecoop.cpp:999-1011 note_terra adds the deltas up; terra_tick sends K_TERRA only `if (here)` (a partner on the same planet, spacecoop.cpp:1026) and clears the sum either way (:1034), so deltas made alone are lost.
* Observed (code): spacecoop.cpp:1052 apply_terra returns when the receiver is not on that planet (`mine != planet_place`), so even a sent delta is dropped by a partner in orbit or elsewhere.
* Observed (docs): docs/re/space.md section 7 ("Partner elsewhere: apply to GetPlanetRecord(id)+0xB0/+0xB4 directly") recommends this case; never done. The BUG-0749 test covers only both ships on the same planet.
* Inferred (high): the same holds host -> guest; no planet-record sync of +0xB0/+0xB4/+0xB8 exists in mods/coop (grep).

## Fix idea
Keep the per-planet sums when no partner is here and send them anyway with the PlanetID; a receiver not on that planet adds them to cPlanetRecord(id) +0xB0/+0xB4 (water +0xB8, clamped as SetScoreB8 does). Better: the host sends its records' terraform values for the empire's planets on change as a host-owned row, so a rejoin also aligns. Twin switch e.g. `space.terra_elsewhere`.

## How to test
space_free, two games: the guest lands on planet 01460035, the host stays in orbit of another star; the guest uses an Atmosphere Generator (coop_space_terra_tool.py's clicks) for 8 s; then the host lands on 01460035. Assert the host's planet record +0xB0 is within 0.01 of the guest's. FAIL now (host 0.0), PASS with the fix. Also the reverse (host terraforms, guest away).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0749. Severity medium, confidence high.
