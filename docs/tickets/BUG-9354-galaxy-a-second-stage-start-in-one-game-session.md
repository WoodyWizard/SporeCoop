---
id: BUG-9354
title: Galaxy: a second stage start in one game session never arrives as the planet's new era
status: fixed
kind: bug
stage: space
reported: 2026-11-23
source: QA (MSI)
role: host
versions: 0.7.14.1-lineages69-dev
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 3535a616
tests: tools/qa_win (manual, 2026-11-23): the MSI's game with every stage reached, TITAN's galaxy server; two stage starts in one session
signature:
docs: docs/12-server-and-players.md#server-space-start
related: BUG-9278, BUG-9336, BUG-9343, BUG-9346
---

## Report
Cross-PC QA (2026-11-23, TITAN's galaxy server, the MSI's game with every stage reached): in one game session the
game started Clyth's Civ era from its Tribe era (the crossing "Clyth~2|Tribe" arrived: "the party's crossing arrives in
Civ"), left, then started Clyth's Space era from the stored Civ era (crossing "Clyth~2|Civ"). The game reached Space
over Clyth (15:27:30) and stayed there, but the crossing never arrived: no "arrives in Space" line, the server kept
"Clyth~2|Civ", no Clyth|Space era. On a fresh game the same Space start had arrived (Abber, BUG-9346).

## Root cause
galaxy_client.cpp: g_arrive_sent (the crossing's ARRIVE is sent once) was reset only by MOVED (a party moving on
by its vote). A start crossing (BUG-9336, 9343, 9346) comes by ROLE like any enter, so after the first crossing of a
session the flag stayed set and every later crossing waited for nothing. Seen in the log above; read in the code.

## Fix
reset_planet_steps() (leaving a planet, the server losing the game, MOVED) resets the flag and its timer. Switch
`coop.fix galaxy.arrive_reset off`: as before.

## Verification
The MSI (profile MSISpaceStart, 1024x768, 10 fps), TITAN's galaxy server, the same two starts in one game session:
Clyth (Tribe) -> Civ (new), leave, Clyth (Civ) -> Space (new).
- FAIL (the build before, b21f63bb, galaxy copy worlds_msi_ship2): the first crossing arrived ("arrives in Civ",
  14:49:59); the second reached Space at 15:27:30 and never arrived (no line; the server kept "Clyth~2|Civ").
- PASS (3535a616, a fresh copy of the base galaxy): "Clyth|Civ: the party ... arrived from its crossing" (15:43:31),
  then "Clyth|Space: the party ... arrived from its crossing and hosts it" (16:06:22).
no-fail-run: Windows QA by hand; the FAIL run is the build without the fix (the switch's off is the same code path).

## History
- 2026-11-23: opened.
- 2026-11-23: fixed on feat/lineages-alpha (3535a616), seen failing and passing on the MSI.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
