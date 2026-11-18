---
id: BUG-1250
title: Creature: the partner's puppet is selectable in the social stance; the guest cannot recruit a creature into its pack
status: open
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1242
---

## Report
A players' video: in the combat stance the partner's puppet could not be clicked; in the social stance it gets the green ring and the name plate, so the players think the partner can be taken into the pack. Then the host recruits a kin creature into its pack, the guest tries the same with another kin, sings for 10 s: nothing ("no, it doesn't work"). Suggested cause: h_HoverFilter (puppets.cpp) passes the partner's puppet only with GetAbilityMode() != 0; social.cpp's apply_success handles only the Impress of other species and has no pack recruit (PosseRecruit) for the guest.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
