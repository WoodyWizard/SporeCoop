---
id: BUG-0276
title: tribe: after a stage change three guests were not in the host's world within 300 s (they got there after a rejoin)
status: fixed
kind: bug
stage: tribe
reported: 2026-10-29
source: lab 20261029-122937 campaign
role: 
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w7-vote
commits: f455e3c
tests: tribe_snapshot
signature:
docs: docs/re/tribe.md#tribe-share-save-dropped
related: BUG-0258, BUG-0112
---

## Report
Lab 20261029-122937 campaign#2 (lane 3, out/lane3/campaign/20261029-122952): after the stage change
to Tribe, "Bob, Carol, Dave did not get into the host's world in tribe within 300 s (together on the
host: [])". The campaign then made each leave and join again; after the rejoins all were together
(12:56:38). Logs: the 0N_stuck_* tails in that folder.

## Root cause
Observed (logs, campaign#2 of lab 20261029-122937): the host entered Tribe at 13:50:18.861. The
shares of the "evolved" save (posted 13:50:16, still in Creature) hit "the game went from Creature
to Tribe during the save: saving again"; the new shares queued at the Tribe entry had no older save
to join, so one of them posted its save at once (13:50:18.929, 68 ms into the stage) and the others
joined it. The game never wrote that save: no "save: objects kept out" line, no "game saved" box.
After 10 s "no saved box ... sharing anyway" sent the slot's files, i.e. the Creature save of
13:50:17 (3577426 bytes, id 0593FBED). Every guest loaded it and played Creature
(`game.mode` 0x01654C01) in the host's planet while the host played Tribe; a rejoin 5 min later
saved in a settled Tribe and worked. In the other two games of the lab the new shares joined the
older save first and its "saving again" delay put the save 2 s after the change: written.
Inferred (not reproduced on purpose): the game drops a save posted in the first frames of the new
stage.

## Fix
world.cpp: a share save waits until the stage has been playable for 2 s (`g_playable_since`); a
share whose save showed no box is sent only when a file of the slot (or one folder below) was
written after the save was posted, else the share saves again (at most three times).
`coop.status` prints both counters. Switch: `coop.fix world.share_settle off`.

## Verification
- no-fail-run: a timing race of the four-player Creature -> Tribe change (a new share must post its
  save within the first frames of the stage, once in three games); not reproduced with two players.
- Regression with the fix: tribe_snapshot (live Creature -> Tribe, two players) lab
  20261029-143946 PASS; vote, vote_stale, vote_caption, ui lab 20261029-143949 PASS.

## History
- 2026-10-29: opened.
- 2026-10-29: analysed from the logs and fixed on fix/w7-vote (share save waits for a settled stage, a stale share saves again); no FAIL run.
