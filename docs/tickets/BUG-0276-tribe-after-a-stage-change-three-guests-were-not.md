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
fixed_in: 0.7.3.1
branch: fix/w7-vote, fix/w8-stuck
commits: f455e3c 1e5edc8
tests: tribe_snapshot, campaign_tribe (coop_campaign.py --from creature --to tribe)
signature:
docs: docs/re/tribe.md#tribe-share-save-dropped, docs/re/tribe.md#tribe-share-settle-reset, docs/06-testing.md#testing-campaign-stage-change
related: BUG-0258, BUG-0112, BUG-0287
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

Wave 8 (fix/w8-stuck), the first fix did not hold: lab 20261029-174717 campaign#1 (lane 2, main
b5ef74d, `world.share_settle` on) had the same finding. Observed: the host entered Tribe at
19:08:38.692; the older shares (saved in Creature) went "saving again", the new shares found no save
to join and one posted its save at 38.767, 75 ms into Tribe; no box, "sharing anyway" 10 s later sent
the slot's Creature files; the guests played Creature (`game.mode` 0x01654C01) for 300 s. Why the
first fix let it through: its clock `g_playable_since` (world.cpp, the via-menu load's) restarts only
when the local player has nothing to control, and the host's creature is the Tribe avatar from the
first frame, so the clock still read the Creature time. The stale check then accepted the Creature
files: written ~0.7 s before the save was posted, inside its 2 s slack. BUG-0258 (the snapshot.spo
name) is not involved: the place was the planet on every side.

## Fix
world.cpp: a share save waits until the stage has been playable for 2 s (`g_playable_since`); a
share whose save showed no box is sent only when a file of the slot (or one folder below) was
written after the save was posted, else the share saves again (at most three times).
`coop.status` prints both counters. Switch: `coop.fix world.share_settle off`.

Wave 8 (1e5edc8), switch `world.share_settle_reset`: the share's own settle clock
(`g_share_settled_since`) restarts at every game mode change and every place change (the adapter's
place, `world::set_place_probe`; also the Space planet -> solar system change, BUG-0287); a share
whose save showed no box is stale also when no file of the slot is newer than the newest file before
the save was posted (`g_saved_base`). The host logs each share save with its settle time.

## Verification
- no-fail-run: a timing race of the four-player Creature -> Tribe change (a new share must post its
  save within the first frames of the stage, once in three games); not reproduced with two players.
- Regression with the fix: tribe_snapshot (live Creature -> Tribe, two players) lab
  20261029-143946 PASS; vote, vote_stale, vote_caption, ui lab 20261029-143949 PASS.

- Wave 8 (fix/w8-stuck), `coop_campaign.py --from creature --to tribe --minutes 2 --no-finale`
  (suite `campaign_tribe`, four players):
  - FAIL with `SPORE_FIX_OFF=world.share_settle_reset` (main's behaviour): lab 20261029-192222
    campaign_tribe#1, lane 7: share save posted 0.2 s into Tribe, no box, "sharing anyway", stuck
    finding "Bob, Carol, Dave did not get into the host's world in tribe within 300 s". (#2 of the
    same lab: the save came 2.0 s in, no bug: a race.)
  - PASS with the fix: lab 20261029-194928 (lanes 2, 4, 7, 4 of 4 OK): campaign_tribe#1 and #2,
    the share saves 2.0-2.1 s into Tribe, all guests together after 25 s; campaign_space#1 and #2
    (regression, BUG-0287) saves 2.1 s into the solar system, together after 95 / 168 s.

## History
- 2026-10-29: opened.
- 2026-10-29: analysed from the logs and fixed on fix/w7-vote (share save waits for a settled stage, a stale share saves again); no FAIL run.
- 2026-10-29: reopened by fix/w8-stuck: the same stuck finding in lab 20261029-174717 campaign#1 with the fix in (its settle clock did not restart at a stage change with an avatar in both stages).
- 2026-10-29: fixed again on fix/w8-stuck (1e5edc8, `world.share_settle_reset`): FAIL run 20261029-192222, PASS run 20261029-194928 (2 of 2 campaign_tribe, 2 of 2 campaign_space).
- 2026-10-29: released in 0.7.3.1.
