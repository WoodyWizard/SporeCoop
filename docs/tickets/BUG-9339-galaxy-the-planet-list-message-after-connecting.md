---
id: BUG-9339
title: Galaxy: the planet list message after connecting runs off the screen's right edge
status: open
kind: bug
stage: menu
reported: 2026-11-22
source: qa-win connect screenshots (2026-11-22)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/12-server-and-players.md#server-planet-details-panel
related: BUG-9337
---

## Report
Seen on TITAN (QA 9, a test galaxy of 6 planets, 1024x768): right after Join on the Co-op window's galaxy row, the
galaxy menu showed a message box in the top right with "Planets: Abber|Civ (0, ..." on one line and "Abber|Creature
(0, no ..." on the next, both cut off by the window's right edge. It went away a few seconds later. With more planets
the line grows longer. Picture: out/shots/connect (not in git), step 4.

## Root cause
Inferred from the code: galaxy_client.cpp, the PLANETS answer, joins every planet into one `say()` line ("Planets:
<name> (<players>, no host, saved) ..."), a console-era message. The galaxy menu shows the planets as stars since
#server-galaxy-menu-built, so the line is noise there. The planet details panel (mockup, 8M) would show the same data
in a readable place.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-22: opened.
