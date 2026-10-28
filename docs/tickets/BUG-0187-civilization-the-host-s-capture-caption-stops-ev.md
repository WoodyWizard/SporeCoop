---
id: BUG-0187
title: Civilization: the host's capture caption stops everybody's game
status: fixed
kind: bug
stage: Civ
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: host
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-civ
commits: f0c5f0a
tests: civ_caption
signature:
docs: docs/re/civ.md#civ-wave3
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* A captured city plays a cinematic whose caption waits for the host's OK; until then the host's
  world (and so every guest's vehicles) stands still. Everybody is told now ("... watches a
  cinematic: the world waits for the OK"), but a host who is away stops the game for all.
  Reproduce: capture a city (`play_civ.py`), do not press Enter on the host. Fix idea: close the
  caption on the host after a few seconds when guests are connected (EndCinematic 0xADFF20).

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Fixed on fix/w3-civ (f0c5f0a): with 2 or more players the host's cinematic in Civ (a capture
caption) is ended after 8 s through the game's `cCinematicManager::EndCinematic` (outside the frame);
everybody is told. The notice says "(or 8 s)". `coop.civ fix caption 0`: the old wait (tests).

## Verification
`coop_civ_caption.py` (suite `civ_caption`, fixture civ_npc): the host captures an NPC city through
the game's Capture (`coop.civ capture`), nobody presses Enter. FAIL with `--nofix` (cinematic state
still set after 14 s, the city not yet the player's on both, a host vehicle does not move), PASS with
the fix (cinematic ended, city on both, the vehicle drives 39.7 m; a screenshot showed the HUD back
without the caption), lane 7.

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fixed on fix/w3-civ (f0c5f0a), test civ_caption (FAIL with --nofix, PASS with the fix).
- 2026-10-28: released in 0.7.3.0.
