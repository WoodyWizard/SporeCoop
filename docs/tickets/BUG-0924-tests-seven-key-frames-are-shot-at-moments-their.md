---
id: BUG-0924
title: tests: seven key frames are shot at moments their note cannot hold (an editor, a cinematic, a pause menu, a camera on a bare slope)
status: open
kind: test
stage: Tools
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257
role: 
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0263
---

## Report
Key-frame review of the final check out/lab/20261111-044257 (rel/0.7.12 2f8f6ba4): frames whose moment makes the note impossible to
judge. None of them shows a game bug by itself; each costs the review a sheet.

* `out/lab/20261111-044257/frames/sheets/037_play_creature_03_end.jpg`: both games are in their Creature editors (the play ends with the evolution);
  the note asks for Creature play with the partner visible.
* `out/lab/20261111-044257/frames/sheets/041_play_tribe_01_start.jpg`: both cameras look at a bare mountain slope 21 m from the chiefs; no village and no
  member is in the picture (only the partner arrow and the HUD can be judged).
* `out/lab/20261111-044257/frames/sheets/002_campaign_02_advance-cell.jpg`, `012_campaign_ct_02_advance-cell.jpg`, `017_campaign_short_02_advance-cell.jpg`:
  the host is still in the letterboxed landing cinematic 50 m from the nest while the three guests play.
* `out/lab/20261111-044257/frames/sheets/001_campaign_01_play-cell.jpg`: the host has the pause menu ("Options") open over a blurred picture: the
  campaign's own noise step (noise_pause / noise_save).
* `out/lab/20261111-044257/frames/sheets/029_play_cell_01_start.jpg`: the guest is still in the letterboxed meteor intro (the tag "Alice" stands on the
  meteor), its own cell is not out yet.
* `out/lab/20261111-044257/frames/sheets/026_find_partners_02_creature_arrow.jpg`: the guest is in the deep-water warning cinematic ("Dangerous creatures
  lurk in the deep"); the arrow itself is there and right.
* `out/lab/20261111-044257/frames/sheets/040_play_space_03_end.jpg`: the host is in a landing cinematic.
* `out/lab/20261111-044257/frames/sheets/043_play_tribe_03_end.jpg`: the host in the game-over caption (its own ticket, BUG-0922).

## Root cause
Observed: `twoinst.keyframe` shoots at once; the play tests shoot "end" after the last action whatever the games
show. Inferred: a key frame needs to wait until no game reports `busy=cinematic`, an editor mode or an open pause
menu (with a time limit), and play_tribe's start frame needs the camera on the village (the minimap view the bots
calibrate with).

## Fix
Open.

## Verification
no-fail-run: tooling (the notes and the moment of the frames). How to test: the same sheets of the next run show
play in every tile.

## History
- 2026-11-11: opened.
- 2026-11-11: second final check (out/lab/20261111-090547, review on plan/w47-frames2): the same moments again:
  sheets 003, 013, 018 (advance-cell: the host in the landing cinematic), 027 (find_partners creature_arrow: the
  guest in the deep-water warning), 030 (play_cell start: the guest in the meteor intro), 042 (play_tribe start:
  both cameras on a bare slope). New: 035 (play_civ end: the host in a trade-route talk box of an NPC nation),
  009 (campaign advance-civ: guest Dave with the co-op window open), 033 (play_civ start: money 33,000 on the
  host and 3,000 on the guest, shot between the test's money gift and its arrival 5 s later). Not this time:
  play_creature end (both in play), play_space end, campaign play-cell.
