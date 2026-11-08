---
id: BUG-0763
title: Space: terrain sculpting and planet colouring tools change the planet in the user's game only
status: open
kind: desync
stage: Space
reported: 2026-11-08
source: fix/w46-b1-space (BUG-0749 exploration, 2026-11-08)
role: 
versions: 0.7.11 and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Seen on fix/w46-b1-space (lane 6, `space_free`, 2026-11-08) while looking for a terraforming tool for BUG-0749: the
tool palette's 6th tab holds 9 terrain sculpting tools (raise / lower / flatten, with 7 sub-categories) and the 7th
tab 8 planet colouring tools plus a reset. The mod relays only the atmosphere / water / temperature deltas
(spacecoop.cpp h_TerraA/W/T, K_TERRA): inferred from the code, a sculpted crater or a recoloured planet appears in
the user's game only. Not run yet.

Reproduce: two games on `space_free`, both on planet 01460035; the host opens the tab at (W-226, H-151) and uses a
sculpting tool on the ground (click the slot, then the ground); compare the terrain height at that spot
(cPlanetModel::GetHeightAt) or a screenshot of both games.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
