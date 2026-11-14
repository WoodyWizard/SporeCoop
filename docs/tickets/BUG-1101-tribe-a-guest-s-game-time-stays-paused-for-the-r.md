---
id: BUG-1101
title: Tribe: a guest's game time stays paused for the rest of the stage after a village conquest cinematic overlaps the host's pause wait (campaign_tribe_noclick: Carol 30 min, Dave 16 min busy=paused)
status: open
kind: desync
stage: Tribe
reported: 2026-11-14
source: final check 0.7.14, lab 20261114-005006 campaign_tribe_noclick
role: guest
versions: rel/0.7.14 4dd8d2be
fixed_in:
branch: fix/w49-fcgates
commits: f1f27e2a
tests: tribe_conquest_guest (the game-time check), coop_tribe_conquest_guest.py --click-at-close
signature: guest trace busy=paused (no input_lock, no cinematic) for minutes after "cinematic 5418B038 still playing in Tribe after 8 s on a guest (BUG-0983): ended it" in the second of a caption click
docs: docs/known-issues.md#issue-guest-caption-click-pause, docs/06-testing.md#testing-fcgates-0714
related: BUG-0983, BUG-1086, BUG-0531
---

## Report
campaign_tribe_noclick of the 0.7.14 final check (lab 20261114-005006, lane 5) failed the sync-check gate: Carol's
proxy of an epic (n204, hp 989) 9.1 m off the host's, then Dave's 8.1 m. The proxy stood still 9 m up (`grounded=0`,
`err` constant for minutes).

## Root cause
Observed (logs): Carol's game time stood (`busy=paused`, no input lock, no cinematic) from s 610 to the end of Tribe
(about 30 min); Dave's from s 1440 (16 min); Bob's never. With the guest's simulation standing, its proxies are only
placed, never fall or walk: the epic made 7 m up stayed there. The host's pause wait (BUG-1086) was balanced on all
guests ("pause is up" / "pause is gone" pairs, `g_go_paused` false). Each stand began in the second where the
campaign's driver clicked the caption's check on that guest and the mod's BUG-0983 forced end of the village caption
ran: Carol "caption ok" 01:01:02 and the end of 5418B038 at 01:01:02.486; Dave "caption ok" 01:14:50 and the end at
01:14:50.030. The six other ends of the run, with the click a second or more away, left no stand.
Inferred: the caption's OK and the forced EndCinematic both end the cinematic; the stage's end listener (the chain to
TRG_Upgrade, its pause) runs twice and one pause count is never lifted. Not confirmed: the reason left was not read
(`coop.pauses`).
Not reproduced: `coop_tribe_conquest_guest.py --click-at-close` (9 clicks 7.5-8.7 s after the caption came up), lab
20261114-031126: no caption at all in 2 of 3 runs, no stand in the third.

## Fix
None yet. tribe_conquest_guest now fails when the guest's game time still stands after the conquest and prints
`coop.pauses`.

## Verification

## History
- 2026-11-14: opened on fix/w49-fcgates (final check 0.7.14 triage); cause inferred from the logs, not reproduced.
