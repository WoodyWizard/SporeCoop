---
id: BUG-9339
title: Galaxy: the planet list message after connecting runs off the screen's right edge
status: fixed
kind: bug
stage: menu
reported: 2026-11-22
source: qa-win connect screenshots (2026-11-22)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 28034591
tests: tools/qa_win (manual, 2026-11-22): TITAN's rebuilt test galaxy (3 planets), QA 9 connecting; coop.fix galaxy.short_planet_list off/on
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
galaxy_client.cpp: the menu's poll writes the whole planet list to the log only; the screen gets one line, "<galaxy>:
N planets, M players online" (planets counted by name, eras and crossings as one). `coop.galaxy list` still answers
with the whole list. Switch `coop.fix galaxy.short_planet_list off`: the whole list on the screen.

## Verification
TITAN, the rebuilt test galaxy (Abber Civ, Clyth Tribe, Samuth Creature), QA 9 at 1024x768, `coop.galaxy connect`:
- PASS (on): "Lineages Alpha Galaxy: 3 planets, 0 players online" under "Connected to the galaxy", whole (images/bug9339_short_list.png);
  the log holds "Planets: Abber|Civ (0, no host, saved) Clyth|Tribe ... Samuth|Creature ...".
- FAIL (off, disconnect and connect again): "Planets: Abber|Civ (0, no host, saved) Clyth|Tribe (0, no host, saved)..."
  cut off after two planets (images/bug9339_off.png).
no-fail-run: Windows QA by hand; the runs above are the switch on and off.

## History
- 2026-11-22: opened.
- 2026-11-22: fixed on feat/lineages-alpha (a short line on the screen, the whole list in the log).
