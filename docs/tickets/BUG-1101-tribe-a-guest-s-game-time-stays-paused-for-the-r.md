---
id: BUG-1101
title: Tribe: a guest's game time stays paused for the rest of the stage after a village conquest cinematic overlaps the host's pause wait (campaign_tribe_noclick: Carol 30 min, Dave 16 min busy=paused)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-14
source: final check 0.7.14, lab 20261114-005006 campaign_tribe_noclick
role: guest
versions: rel/0.7.14 4dd8d2be
fixed_in: unreleased
branch: fix/w49-pause
commits: 41d85d9f, 73aeec65, f1f27e2a
tests: tribe_pause_leak, tribe_caption_race, tribe_conquest_guest (the game-time check)
signature: guest trace busy=paused (no input_lock, no cinematic) for minutes after "cinematic 5418B038 still playing in Tribe after 8 s on a guest (BUG-0983): ended it" in the second of a caption click
docs: docs/known-issues.md#issue-guest-caption-click-pause, docs/06-testing.md#testing-fcgates-0714, docs/06-testing.md#testing-pause-leak, docs/re/tribe.md#tribe-caption-pause-leak
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

Further analysis (fix/w49-pause, docs/re/tribe.md#tribe-caption-pause-leak):
Observed (runtime, `coop.pauses trace`): the guest's conquest caption pauses nothing; the queued TRG_Upgrade pauses
0x4BF38A5 at its start and 0x600C4AE from its script's pause step (6.46 s), its end resumes it; the host's caption
counts as a host pause (BUG-1086 wait, 0x4BF38A7). Every Pause had its Resume in 9 conquests with the guest's click
7-9.6 s into the caption, with the caption's OK / skip messages sent or posted before / after the forced end in the
same call, with 120 / 250 ms frames and 1.2-2.6 s stalls around the pause step.
Observed (Ghidra): one way the game leaves a pause counted: the cinematic manager's pause slot (+0x160) is set by a
script's pause step with no check that a cinematic plays and cleared without a Resume by the next cinematic's start
(0xAD8A90); the time manager's counts go under 0 on a stray Resume.
Not confirmed: which reason the campaign's guests kept (no trace in that run). Not the mod's waits (balanced).

## Fix
Safety net `tribe.guest_pause_net` (tribe.cpp pause_net_tick, guest, Tribe): the game time standing 20 s with only
the reasons 0x4BF38A5 / 0x600C4AE / 0x4BF38A7 counted and no cinematic, input lock, modal window or host wait is
released (the cinematic's slot by its own resume message 0x46AC986, the rest by Resume) and logged with the reasons;
the player's own P (0x4BF38A8) and pause menu (0x4BF38A4) are never released; a released reason that its owner
resumes later is counted back to 0. tribe_conquest_guest fails when the guest's game time still stands after the
conquest and prints `coop.pauses`.

## Verification
- tribe_pause_leak FAIL with SPORE_FIX_OFF=tribe.guest_pause_net: lab 20261114-041620 (lane 5: the planted pause
  stood the whole test). PASS: 20261114-041605, 20261114-042104 (lane 7).
- Related, PASS: tribe_caption_race, tribe_gameover_wait (20261114-042104, lane 7); tribe_conquest_guest,
  tribe_host_pause, tribe_hut_defeat_wait (20261114-042119, lane 5); the net released nothing in them.

## History
- 2026-11-14: opened on fix/w49-fcgates (final check 0.7.14 triage); cause inferred from the logs, not reproduced.
- 2026-11-14: fix/w49-pause: pause trace, the cinematic pause slot RE, not reproduced; safety net tribe.guest_pause_net, test tribe_pause_leak.
