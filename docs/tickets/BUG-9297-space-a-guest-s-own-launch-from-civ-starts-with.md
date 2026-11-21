---
id: BUG-9297
title: Space: a guest's own launch from Civ starts with the host's captain and money until the host's world loads
status: open
kind: bug
stage: Space
reported: 2026-11-21
source: lane C test (BUG-9295)
role: guest
versions: 0.7.15.2
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9295
---

## Report
Seen in the BUG-9295 test (lane C, Kealeneon): after the guest's own launch (its own ship editor, its own game into
Space) the guest's planet-side Space start is its local copy of the host's Civ save: the HUD shows the host's captain
(the host's species, "Keigh Emp...") and the save's money (88,888 on the fixture; the seat's own money is the same number there, so the money part is not confirmed yet). Only when the guest leaves its home
planet and the host's world loads does space_empires set the guest's own captain (7CA81684), money and empire name.
Reproduce: agentC4/t1.py (batch folder), screenshot after "guest: its own game launches into Space".

## Root cause
Inferred: space_empires.cpp applies the seat's own empire (captain, money, name, colour) on a world received from the
host; the guest's own local Space start (civ_launch.cpp) is not such a world, so the save's player empire (the host's)
is shown. A fix would apply the seat's empire to the local start too (same code path, the seat is known).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-21: opened.
