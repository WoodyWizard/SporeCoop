---
id: BUG-9291
title: Era visitors: another era's vehicles and buildings drawn as props are not reliably visible
status: open
kind: feature
stage: creature
reported: 2026-11-20
source: step 4 part 5 (Civ era visitors)
role: any
versions: 
fixed_in:
branch: feat/lineages-alpha
commits:
tests:
signature:
docs: docs/12-server-and-players.md#server-era-visitors-more
related: BUG-9290
---

## Report
Another era's vehicles and buildings are made as ground props at the right places (status: alive, on the
ground) but mostly do not show. A truck prop showed once (a fresh game at 1280x960, near the nest, as in the spike of
2026-11-17), and not after the avatar was moved, nor in any 800x600 game, nor after a hot reload.

## Root cause
Open. Ruled out: the bake (asked and done: all levels baked, still not shown), the session (offline
too), the mounted package (plain launch too), the carrier (a UFO carrier did not show either), the teleport alone (not
shown at the nest without one). Next: what the game does to show an ornament it makes (City_MakeDecorations 0xBE6A50,
the planet record's ornaments 0xFF96B0, spatial +0x70), the screen size, and a fresh game per try (the test save's
creature dies at load: restore its save first).

## Fix
Not yet.

## Verification
no-fail-run: open.

## History
- 2026-11-20: opened.
- 2026-11-20: opened with the runs that rule things out.
