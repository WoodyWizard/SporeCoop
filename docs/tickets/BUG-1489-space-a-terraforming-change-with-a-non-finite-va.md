---
id: BUG-1489
title: Space: a terraforming change with a non-finite value from a peer is applied to the active planet (SPC-15)
status: open
kind: bug
stage: Space
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w77-space
commits: e9c0dd3d, b8cd7f35, 37d9240d
tests: space_terra_nan, space_terra_nan_off, space_tests (make test)
signature:
docs: docs/re/space.md#space-audit-0717
related: BUG-0749, BUG-0883, BUG-1042
switch: space.terra_finite
---

## Report
External static audit of 0.7.15.5, item SPC-15 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-X1 (fix/w77-space). A behaviour change gets a switch and a twin.

## Root cause
Inferred from code, confidence high (cheat-by-modified-peer class): spacecoop.cpp apply_terra called the game's
Add*Delta with every `d[i] != 0`; NaN != 0 is true and nothing checked finiteness (wire/space_core.h leaves it to the
handler, only apply_terra_rec checked). The delta also switches the planet's terraforming flag on, and the terrain tick
integrates it into the active planet's record (+0xB0 / +0xB4 / +0xB8), which the world saves. An honest game sends
deltas of its tools only.

## Fix
A K_TERRA with any non-finite value is refused (`terra_refused`), finite ones are clamped to +-1
(`spacelogic::terra_delta`, kTerraDeltaMax: more than the record's whole 0..1 range never comes from a tool). Switch
`coop.fix space.terra_finite off`: 0.7.16.1. Test seam: `coop.space terrasend <place hex> <a> <w> <t>` (nan, inf).

## Verification
open, waiting for the lab run.
`coop_space_terra_nan.py` (suite `space_terra_nan`, twin `space_terra_nan_off`, fixture space_free): both land on the
colony planet; an honest delta reaches the guest; NaN and infinite deltas leave the guest's planet record finite over
12 s and are counted refused. Expected: PASS with the fix; FAIL with SPORE_FIX_OFF=space.terra_finite (record NaN).
pending night run: space_terra_nan space_terra_nan_off (1 lane, about 12 min).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-X1), branch fix/w77-space.
- 2026-11-23: fix, lab test and FAIL twin on fix/w77-space (WP-X1); no lab run yet (lab runs only at night).
