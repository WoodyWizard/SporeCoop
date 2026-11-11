---
id: BUG-0922
title: Tribe: the host sits in the game-over caption for the last 40 s of play_tribe while the guest plays on and sees the host's chief standing; the test's game-over rule does not see it
status: open
kind: bug
stage: Tribe
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257
role: host
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0172, BUG-0685, BUG-0652
---

## Report
Key-frame review of the final check out/lab/20261111-044257 (rel/0.7.12 2f8f6ba4), play_tribe (lane 5, seed 6749893, result ok).
Sheets: `out/lab/20261111-044257/frames/sheets/043_play_tribe_03_end.jpg` (the finding) and `out/lab/20261111-044257/frames/sheets/042_play_tribe_02_mid.jpg` (before).
Frames: `out/lab/20261111-044257/frames/play_tribe/03_end_host.jpg`, `03_end_guest.jpg`.

* Host: a letterboxed cinematic over a dark meadow with the epic creature, the caption "Alas! Your population has
  fallen below the minimum needed to survive. Will this tragic story repeat itself? Bounce back in time to find out!"
  and its OK button; no HUD. The tag "Alice's chief" stands at the left edge over dark ground.
* Guest: normal Tribe play at the village: the host's chief stands at the hut with its staff under the tag "Alice's
  chief", HUD, minimap, a roster of 2.
* Host log: `Tribe, cinematic` from 05:48:21 (s 589), `busy=cinematic,input_lock,paused` in every trace line up to the
  end of the log (05:49:02, s 630): the host sat in the caption for 40 s and more while the guest played on. Before it
  the host's member count fell 9 -> 7 -> 6 in the epic phase (the epic came at s 480). Guest log 05:48:24: "mirror #1
  goes: dead here, back on its feet on the host (made again, BUG-0685)".
* The test says "game overs 0" and every check passed.

## Root cause
Observed (tools/tests/play_tribe.py, the game-over rule): it presses Enter only when no member with tribe 'p' and
hp > 0 is left on the host for 15 s. Inferred: the guest's members belong to the player tribe in the host's game
too, so with a living guest the list is never empty and the rule never fires; the host stays in the caption until
the play ends. Not known: why the game showed the caption with 6 members of the player tribe alive (the host's own
members and chief all down at once, by BUG-0172's notes the caption comes when only the knocked-out chief is left),
and whether the guest should show something while the host is in it (today it plays on against a paused host).
Probably older than 0.7.12 (the epic phase and the rule are not new); seen once, in 1 of 5 runs with an end frame.

## Fix
Open.

## Verification
Open. How to test: replay `SPORE_LANE=5 python3 tools/tests/play_tribe.py --minutes 10.0 --seed 6749893`; or kill the
host's own members by `coop.tribe pincer` while the guest has members (BUG-0172's recipe) and read `coop.status` busy
flags and `coop.ui find "Alas"` on both. The check the test lacks: the host is not in a cinematic with a caption for
more than 15 s (`coop.ui find "Alas"` instead of counting members), and the end key frame only after it is closed.

## History
- 2026-11-11: opened.
