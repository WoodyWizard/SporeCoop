---
id: BUG-0541
title: Space: 9 ornaments by one colony city exist in one game only (Space:guest_only:ornament / host_only:ornament)
status: open
kind: desync
stage: Space
reported: 2026-11-04
source: divergence detector (space_play, out/lab/20261104-104326 of fix/w42-space-planets; rel/0.7.5 out/lab/20261104-013517)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Detector: `Space:guest_only:ornament` (9 ornaments, noun 0x18C88E4, near city 0 of the home planet at (146 325 -362), on
the guest only) once in space_play (out/lab/20261104-104326, after the guest's death and respawn at that city);
`Space:host_only:ornament` (9, the same spot, on the host only) once in rel/0.7.5 out/lab/20261104-013517. Found by the
lab (no player report). How to see it: `space` fixture, both land (`coop.space land`), `coop.census all 3000 0 0 0` on
both: lane 3 2026-11-04 the host had 14 ornaments (9 within 80 m of city 0 (153 314 -368)), the guest 5 (0 there), and
putting the guest's ship over city 0 (`coop.space over 153 314 -368`, 20 s) made none there: not the camera's level
of detail. The whole-list counts differed in other sessions too (32 / 41).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not traced. Inferred: the planet load 0xFF96B0 makes a noun 0x18C88E4 object for every planet-record item with flag
0x80 (docs/re/space.md#space-planet-life); which game has the items (or other makers of this noun) differs.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
