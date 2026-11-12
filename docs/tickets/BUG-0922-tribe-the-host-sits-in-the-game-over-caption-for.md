---
id: BUG-0922
title: Tribe: the host sits in the game-over caption for the last 40 s of play_tribe while the guest plays on and sees the host's chief standing; the test's game-over rule does not see it
status: fixed
kind: test
stage: Tribe
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257
role: host
versions: 0.7.12 (unreleased)
fixed_in: unreleased
branch: fix/w48-tribe-mirrors
commits: f2051355, b059baac
tests: tribe_gameover_partner, play_tribe
signature:
docs: docs/06-testing.md#testing-tribe-mirror-looks
related: BUG-0172, BUG-0685, BUG-0652, BUG-0960
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
Proven (lab, tribe_gameover_partner): a test artefact plus the game's own rule. The game shows the caption on the host
when every member of the player tribe is down (the guest's members count: with the host's own members and chief
knocked out and the guest's alive there is no caption for 80 s). A knocked-out chief stands up again about 95 s after
its fall with full health (hp 168, dead 0), also while the caption is up and the game is paused behind it; the
caption stays. play_tribe pressed OK only when no player-tribe member had hp > 0 for 15 s: with the chief standing it
never did, and the host sat in the caption to the end of the play while the guest played on (the guest's log "mirror
#1 goes: dead here, back on its feet on the host" 3 s into the caption, its end frame the host's chief standing).
Reproduced: the chief knocked out 80 s before the rest, the caption 10 s after the last fall, the chief up 168 hp a
few seconds later, the members rule never fired in 40 s (out/lab/20261112-093040, key frame = the report's picture).

## Fix
Tests only (no game change): play_tribe's game-over rule is the caption itself (`play_tribe.GameOver`: the window
"Alas..." and every parent visible for 3 s: OK); the play closes a caption left at its end before the `end` key frame
and fails when the host sat in the caption for 25 s. `PLAY_TRIBE_OLD_GAMEOVER=1` keeps the members rule (the twin).
The guest playing on during the host's caption is BUG-0960.

## Verification
Test `tools/tests/coop_tribe_gameover_partner.py` (suite `tribe_gameover_partner`).
* FAIL with `PLAY_TRIBE_OLD_GAMEOVER=1`: out/lab/20261112-093040 (caption 10 s after the last fall, the chief up, no
  OK in 40 s).
* PASS: out/lab/20261112-093429, out/lab/20261112-095407 (the chief stood up in the caption both times, OK at 13 s).

## History
- 2026-11-11: opened.
- 2026-11-12: fixed (fix/w48-tribe-mirrors): a test artefact; play_tribe answers the caption; BUG-0960 opened for the guest's side.
