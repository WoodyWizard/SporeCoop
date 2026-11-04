---
id: BUG-0524
title: Tribe: a tool a guest buys in its palette is made in its own game only; the host has no rack there and the guest's members cannot take it
status: open
kind: bug
stage: Tribe
reported: 2026-11-03
source: fix/w41-tribe-actions (found while fixing BUG-0514)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0514
---

## Report
Found while fixing BUG-0514 (not run): the palette's placement (0xD13C05 / finish 0xD10F90) makes the tool in the
buyer's game. With BUG-0514's fix the host's tools reach the guests (worldobj kind tribe_tool), but a tool a guest
buys exists in the guest's game only: the host's game has no tool there, so a guest's GrabTool order on it is
rejected on the host ("rejected: no such target here") and the host's players never see it. The food is shared.
Wanted: a guest's purchase is a request to the host (the host makes the tool at that slot, the guest's own copy is
bound to it). Reproduce: needs a fixture whose tribe palette is not locked (the 'tribe' fixture's is).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
