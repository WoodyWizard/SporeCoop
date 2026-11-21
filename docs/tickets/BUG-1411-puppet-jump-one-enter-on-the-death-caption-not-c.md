---
id: BUG-1411
title: puppet_jump: one Enter on the death caption, not checked: the guest's game stayed dead and paused 77 s, no respawn, no jump to judge
status: open
kind: test
stage: Creature
reported: 2026-11-21
source: lab rel/0.7.16 20261121-003202 puppet_jump
role: 
versions: unreleased
fixed_in:
branch: fix/w62-flake-cc
commits:
tests: puppet_jump
signature:
docs:
related: BUG-0499, BUG-1376
---

## Report
0.7.16 third final check (lab 20261121-003202, lane 2, warm): puppet_jump FAIL "a creature jumped on its own game at
least once (out of the egg: 0)"; before it "guest: its own game moved its creature by over 1.5 m in 0.5 s at []" and
"guest died: 5 frames of its puppet on the other game" (passing runs: [(0.5, 5.0), (10.2, 5-8)] and 1100-1900 frames).
Known flake (7 of 19 on rel/main); fix/w42-creature-left 20261104-114050 failed with the same line.

## Root cause
Observed (the guest's log of this test's session, from 05:53:48): the own creature 290225CD swapped in at s 13.16, the
lethal hit at s 14.57 (`ko: own avatar dead`, hp 0/16), then every trace line until s 90 has hp 0/16, flags 0x11 and
`busy=cinematic,input_lock,paused`; no `drama: baby_game event starts`, no `alive again`, and at s 92 "own avatar
065CBE70 waits: cinematic F0428C29 plays". The passing runs on rel/0.7.16 (20261120-133703, -041304, 20261119-214823)
have the same swap-to-death timing (1.1-1.4 s) and are paused at s 20 too, then `baby_game` and `alive again` ~8 s after
the death (the caption answered). The test found the caption ("the death caption came and was answered") and sent ONE
Enter without checking that the caption went away. Inferred: that Enter did not close the caption (sent while it was
still coming up, or taken by something else), the game stayed in the death caption's pause, the creature never
respawned and never stepped out of the egg: the test had no jump to judge. Not a mod change of 0.7.16, not the w61
drive.lead_gap (the guest's puppet was hidden as dead on the host the whole time: no states of a living body to lead).

## Fix
Test: `answer()` presses Enter while the caption is up (every second, up to 20 s, Esc on a pause menu it opened, as
coop_creature_respawn.py does) and says when it took more or fewer than one; die() fails when the caption stays.
If a run shows "still up after N Enter(s)", the caption does not take input: then a mod ticket.

## Verification
no-fail-run: test-only change. Lab: `SPORE_DETECTOR=20 python3 tools/lab.py run puppet_jump --repeat 4 --lane N --retry
0 --no-build` PASS (watch for "the death caption took N Enter(s)"). Open, waiting for the lab run.

## History
- 2026-11-21: opened from the third final check of 0.7.16 (fix/w62-flake-cc); fixed in the test.
