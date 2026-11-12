---
id: BUG-0924
title: tests: seven key frames are shot at moments their note cannot hold (an editor, a cinematic, a pause menu, a camera on a bare slope)
status: fixed
kind: test
stage: Tools
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257
role: 
versions: 0.7.12 (unreleased)
fixed_in: unreleased
branch: fix/w48-rest
commits: 9e7abf21
tests: keyframe_settle, play_tribe, same_view_tribe, play_civ
signature:
docs: docs/06-testing.md#testing-w48-rest
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
tools/tests/twoinst.py `keyframe(..., settle=None)` / `keyframe_settle`: before the shot it waits (at most
SPORE_KEYFRAME_SETTLE s, default 20) until no game of the frame reports `busy:` cinematic, paused or loading
(`coop.status`), nor an editor mode (`game.mode`); a game still busy then and a wait of 1 s or more are written into
the manifest's `extra`, so the reviewer sees why a frame shows an editor. Frames meant for a busy moment pass
`settle=0`. play_tribe turns each view to its own members before `start` (the bare slope); play_civ shoots `start`
after the money reached the guest. Left as they are, with the manifest saying so: an editor the play itself ends in
(play_creature end), a talk box of an NPC nation (input lock only), the co-op window.

## Verification
Test `keyframe_settle` (tools/tests/coop_keyframe_settle.py): the guest's pause menu open for 6 s while a key frame is
asked for: the frame must come after the menu closed.
FAIL (twoinst.py before the fix, run from a temporary worktree, copied here): out/lab/20261112-141542 (the frame at
+0.4 s, the menu closed at +6.2 s).
PASS: out/lab/20261112-141721, out/lab/20261112-142543 (the frame at +6.5 s, "waited 6.1 s").
The key frames of play_tribe and same_view_tribe of the final runs (out/lab/20261112-145017): play_tribe start shows the village and the members on both; none in a cinematic, a pause or an editor.

## History
- 2026-11-11: opened.
- 2026-11-11: second final check (out/lab/20261111-090547, review on plan/w47-frames2): the same moments again:
  sheets 003, 013, 018 (advance-cell: the host in the landing cinematic), 027 (find_partners creature_arrow: the
  guest in the deep-water warning), 030 (play_cell start: the guest in the meteor intro), 042 (play_tribe start:
  both cameras on a bare slope). New: 035 (play_civ end: the host in a trade-route talk box of an NPC nation),
  009 (campaign advance-civ: guest Dave with the co-op window open), 033 (play_civ start: money 33,000 on the
  host and 3,000 on the guest, shot between the test's money gift and its arrival 5 s later). Not this time:
  play_creature end (both in play), play_space end, campaign play-cell.
- 2026-11-12: key frames wait for the games to settle (twoinst.keyframe), play_tribe / play_civ start frames, fix/w48-rest.
