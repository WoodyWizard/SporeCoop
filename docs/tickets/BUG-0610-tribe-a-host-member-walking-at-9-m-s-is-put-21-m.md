---
id: BUG-0610
title: Tribe: a host member walking at 9 m/s is put 21 m away by the host's own game for one frame step (the guest follows 0.25 s later)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-06
source: fix/w45-remote-bodies body_trail_tribe_player
role: host
versions: 
fixed_in: 0.7.9
branch: fix/w45-remote-bodies-2
commits: db4b6090
tests: body_trail_tribe, body_trail_tribe_player, tribe_chiefs_apart
signature: host log `tribe: <name>'s chief stood next to another one at the arrival: put on its own spot round the village` within 1 s of a 9-22 m one-frame step of a walking member
docs: docs/re/tribe.md#tribe-place-chiefs-walking
related: BUG-0440, BUG-0425
---

## Report
body_trail_tribe_player (lab 20261106-014554, track out/lane9/track/body_trail_tribe_20261106-014937.json, not in
git) and body_trail_tribe_player of 20261106-014056: the host's member t2, walking at 6-9 m/s on a `coop.tribe order
<id> 4` walk, moved 21 m between two host frames (204-213 m/s for 3 frames of `coop.bodyrec`), then walked on at
6-9 m/s. The guest's mirror stayed on the old path 0.25 s (20.3-21.3 m off) and was then put there (one 21.4 m step:
drive::step's snap). Both runs: t2, 20-22 m.

## Root cause
CORRECTION (2026-11-06, fix/w45-remote-bodies-2): not the host's own game. Observed: the mod's own arrival chief placing (`place_chiefs`, tribe.cpp, switch tribe.place_chiefs, BUG-0340) teleported the guest's chief (t2). Every one-frame jump of t2 on the host in M1's 13 body_trail_tribe tracks (9-22 m) fell within ~1 s of the host's log line "tribe: Bob's chief stood next to another one at the arrival: put on its own spot round the village" (one line and one jump per run; e.g. 20261106-014554 body_trail_tribe_player, track 014937: jump at +11.06 s of the recording, line at 01:48:46.584). `place_chiefs` looks at every chief for 60 s after an arrival (every 2 s) and puts one that stands closer to another chief than their radii + 1 m on its ring spot (teleport + DoAction 4 walk there), with no speed check: a chief walking past the host's chief on its player's order was put away. A first fix that only skipped walking chiefs still put one that had walked next to the host's chief and stopped (13.8 m, lab 20261106-041542). No game-side teleport of a walking citizen was found (static: the 41 indirect vt+0x44 calls in 0xC00000-0xDB0000 do not resolve to the creature Teleport 0xC0C040). The guest following it 0.25 s later is right (the next state).

## Fix
`tribe.place_still` (tribe.cpp place_chiefs): a chief is placed only while it stands (< 0.3 m/s) AND has not moved more than 1.5 m since its first look in this arrival's window (a chief stacked at the arrival stands where it came in); a chief that walked is left alone for the rest of the window (counter `place_walking` in `coop.tribe coop`). The log line carries the game's time (`t <s>`, the clock of coop.bodyrec). Off (`coop.fix tribe.place_still off`): 0.7.8.

## Verification
body_trail_tribe now checks one-frame steps over 5 m of a member on both games' own rows (`body_trail.steps`).
- FAIL: out/lab/20261106-042658 (SPORE_FIX_OFF=tribe.place_still: host t2 19.1 m in one frame at 2.7 s, guest 20.3 m at 3.0 s); also 20261106-041542 (the first, walking-only version: 13.8 m). Offline on M1's tracks: 11 of 13 have such a step.
- PASS: out/lab/20261106-042100 (body_trail_tribe OK, no step; tribe_chiefs_apart OK: BUG-0340's chiefs still put apart).

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-remote-bodies-2, M2): root cause is the mod's place_chiefs (static RE + log/track timing); fixed with tribe.place_still; FAIL 20261106-042658, PASS 20261106-042100.
- 2026-11-07: released in 0.7.9.
