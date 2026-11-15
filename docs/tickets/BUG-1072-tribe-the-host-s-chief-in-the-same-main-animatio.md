---
id: BUG-1072
title: Tribe: the host's chief in the same main animation at the same moment on both games still stands another way on the guest (bent vs upright, the staff at another angle)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-13
source: fix/w49-tribe3 final check, lab 20261113-070558 #3 view 4, 20261113-063317 #3 view 1
role: guest
versions: 0.7.13 - rel/0.7.14
fixed_in: 0.7.14
branch: fix/w49-chief
commits: 5ea0bc18
tests: tribe_chief_tracks
signature:
docs: docs/re/tribe.md#tribe-chief-tracks, docs/06-testing.md#testing-gameover-partner-timing
related: BUG-1069, BUG-0988
---

## Report
After BUG-1069's fix the stills of same_view_tribe show the host's chief in the same main animation at the same
moment on both games (the test prints both games' rows at each still), but in two stills its body still differs:
* lab 20261113-070558 #3 view 4: both games `anim=02481DE5` (the idle, a loop) t 7.24 / 7.21, hold 0: on the host the
  chief stands bent with the staff low to the left, on the guest upright with the staff held out.
* lab 20261113-063317 #3 view 1: both `anim=04C6CB02` t 1.43 / 1.46, hold 0: the staff slanted on the host, upright
  on the guest.
Not analysed. Candidates: the other playing tracks of the animation state (only the main track, +0xF0C[+0xFA4], is
relayed and timed: docs/re/tribe.md#tribe-anim-queued-start lists up to 16 playing tracks), their blend weights, or a
look-at / IK target the mirror does not share.
Reproduce: `lab.py run same_view_tribe --repeat 3`, compare the chief at each still with the `host's chief at the
still:` lines; dump every playing track (id, time, weight) of the chief on both games at that moment.

## Root cause
Observed (probe on lane 7: both games' chief animation state read with `mem.read` at 2.5 Hz for 150 s; Ghidra for the
track fields, docs/re/tribe.md#tribe-chief-tracks):
* The chief plays two or three tracks at once on both games: the main one (idle 0x02481DE5, staff gesture 0x04C6CB02,
  ...) plus a looping layer 0x05EF4EE1 (a ~2.03 s pass) and at times a looping 0x0354559B, the same ids on both games
  (each game starts them itself).
* Their times differed: 0x05EF4EE1 by 9.4 / 20.2 / 31.7 s (1.3-1.9 s within its 2 s loop), 0x0354559B by 0.5-3.2 s,
  and the idle (main, but left out of kind 15 as locomotion) by 0.4-6.8 s; the main gesture matched within 0.03 s
  (BUG-1069's kind 15). Only the main track was relayed, so "the same main animation at the same moment" still blended
  another moment of the layer and the idle.
* Key frames of the twin (lab 20261113-150319, the same camera at the chief): the staff at another angle and the arm
  elsewhere on the two games, as in the report's stills.
Inferred: the pose follows the tracks' times (no bone read); the blend weights are the fades (+0xC0 / +0xC4), the same
on both games in the probe (st=2, playing).

## Fix
`tribe.mirror_phase_tracks` (mods/coop/tribe.cpp): once a second the host sends every member's other playing tracks
(not fading out) and its idle when that is the main track (MSG_NPC_ANIM kind 16, kind 15's entry format); a guest whose
mirror plays a track of that id gives it the host's loop setting, passes and time + RTT / 2 (when 0.1 s or more off).
`coop.tribe anims`: `tracks fix= sent= rx= set=`. Switch `coop.fix tribe.mirror_phase_tracks off`: only the main track.

## Verification
Test `tribe_chief_tracks` (tools/tests/coop_tribe_chief_tracks.py): both games' chief tracks read every 0.4 s for 90 s;
the shared non-main tracks and the idle within 0.3 s in 85 % of the samples.
* FAIL (SPORE_LAB_FIXOFF=tribe_chief_tracks=tribe.mirror_phase_tracks): out/lab/20261113-150319 (34 %: 05EF4EE1 57
  of 169, 0354559B 5 of 56, the idle 40 of 76).
* PASS: out/lab/20261113-150637 #1 (98 %), #2 (96 %; 05EF4EE1 190 of 192, the idle 117 of 119, 0354559B 17 of 25).
* Regression: out/lab/20261113-151144 (tribe_hold_only, tribe_mirror_stale, tribe_chief_still, same_view_tribe all OK; same_view_tribe's stills: the chief's idle 0x02481DE5 within 0.06 s on both games at three of four, the fourth taken in a walk start).
Left: 0x0354559B still off in some samples (restarted on one game between two sends); the key frames of the PASS runs
show the same staff angle with small differences of the free arm (the shots are not taken at the exact same moment).

## History
- 2026-11-13: opened.
- 2026-11-13: cause found (the chief's other tracks run unsynced) and fixed on fix/w49-chief (tribe.mirror_phase_tracks).
- 2026-11-14: released in 0.7.14.
