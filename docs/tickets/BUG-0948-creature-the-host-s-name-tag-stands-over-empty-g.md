---
id: BUG-0948
title: A partner's name tag (and arrow) leaves its creature while the own camera turns: the tag over empty ground, no creature under it (play_creature mid)
status: open
kind: bug
stage: Creature
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-151535
role: guest
versions: 0.7.11.2 0.7.12 (unreleased)
fixed_in:
branch: fix/w47-tag-empty
commits:
tests: tag_camera
signature:
docs: docs/08-ui.md#ui-find-each-other
related: BUG-0907, BUG-0929
---

## Report
Key-frame review of the third final check out/lab/20261111-151535 (rel/0.7.12 8bdf305a), test play_creature
(lane 5, 2 players, the guest plays its own creature), the "mid" frame about 150 s into the play.
Sheet: `out/lab/20261111-151535/frames/sheets/038_play_creature_02_mid.jpg`. Frames: `out/lab/20261111-151535/frames/play_creature/02_mid_guest.jpg`,
`02_mid_host.jpg`.

* Guest's screen: the name tag "Alice" (the host's colour) stands in the middle of an open grass slope, right of the
  guest's own creature (frame x 540, y 235 of 960x720). There is no creature under it or near it: the ground below
  the tag is short grass down to the flowers. The only creature of the host's species in the picture is far left
  (frame x 30..130, y 330..370), low in the flowers, about 450 px from the tag.
* Host's screen at the same moment: the host's creature stands upright in the flowers 2-3 m in front of the guest's
  creature and to its right; the guest's creature has its tag "Bob" over its head.
* The manifest has the two avatars 7.4 m apart (host -111.6 -469.0 -148.4, guest -116.3 -466.6 -153.6).
* The other Creature frames of the run (play_creature start and end, same_view 1 to 4, one_creature, appeared_once)
  have the tag over the partner (high over it: BUG-0929).

Severity: low to medium (the tag is how a player finds the partner). Confidence: high for what is on the frame; one
frame, so how long it lasts is not known. The test's own checks and the detector passed at that moment (findings 0).
Regression of 0.7.12: unknown; the tags and arrows are 0.7.10 code (find_partners) reworked in 0.7.12 (find.placed,
BUG-0939).

## Root cause
Found 2026-11-11 (fix/w47-tag-empty, lane 6). Not a regression of 0.7.12: the tag is drawn with a camera that is not
the picture's camera while the camera turns, and it has been so since the tags exist.

Proven:
* The tag is anchored to the body the game draws. `on_overlay` (mods/coop/coop.cpp) reads the puppet's own position
  (`modes::entity_pos` -> `creature_fill` -> `game::position`, cSpatialObject +0xC4) every frame, in 0.7.12 and in
  v0.7.11.2 alike (0.7.12 added the colour, `find.placed` that only skips a tag, and the Tribe rule of BUG-0907).
  * The model is where the object is: 7550 reads on the guest over 611 s of play_creature (lab 20261111-190822:
    object position against `Anim::AnimatedCreature` +0x04 through `mem.read`), largest difference 0.19 m.
  * The anchor is where the body was drawn in that frame: `coop.track` on both games over the whole play_creature
    (lab 20261111-192238), the position after the frame's driving (what the tag of that frame uses) against the
    position before it (what the game drew): guest 36570 frames, p99 0.13 m, one frame of 1.2 m; host 37650 frames,
    p99 0.11 m, one frame of 6.3 m (a teleport of the puppet). So no stale object, no hidden puppet, no trailing
    model: in world space the tag was at the puppet in all but two single frames of 20 minutes.
* With a turning camera the tag is drawn far from that body. `tag_camera` (tools/tests/coop_tag_camera.py): both
  players stand 5-7 m apart, the guest's camera is turned by the right-drags the play bots use (400 px in 8 frames),
  the guest is shot inside the turn and the tag is compared with the body in the picture.
  * 0.7.12 (lab 20261111-194717): camera at rest 3 of 3 shots the tag over the body (+6 px); of 12 shots inside a
    turn 2 over the body, 2 off it (228 and 240 px of 1440), 2 with the tag over empty ground, 6 without a tag (the
    partner outside the view or an arrow). `frames/tag_camera/tagcam_turn_6.jpg` and `tagcam_turn_12.jpg` are this
    ticket's picture: "Alice" over empty grass, the red creatures elsewhere (the same two shots in lab
    20261111-194149). In lab 20261111-193855 `tagcam_turn_10.jpg` has the host's creature in the middle of the
    picture (the game's own rollover on it) and our arrow "Alice 7 m" at the screen's edge.
  * v0.7.11.2 (a build of the tag in a temporary worktree, the same test with `--colour 80FF80`, lab 20261111-194417
    of that worktree, removed with it; three of its shots are in `out/lab/20261111-194417-v0.7.11.2/` of this
    worktree): at rest the tag over the body, of 12 shots inside a turn 5 with the tag off its body (70 to 313 px
    of 1440; in `old_turn_9.jpg` the green "Alice" stands 190 px of 960 beside the creature) and 7 without a tag.
* The frame of the report fits: the tag is at the height it has over the red creature and only moved sideways (a
  yaw), the guest stood still (its trace positions at 16:15:15 and in the manifest are the same) and its bot turns
  the camera all the time (`guest.look_around=211` in that 10 min run, each a drag of 300-450 px over 8 frames and
  more: several per cent of the guest's frames).

Why the two cameras differ (read in the code, not measured further): the overlay pass runs in SporeKit's Present
hook after the frame is drawn (sporekit/d3d9hook.cpp: `frame_common`, then `SK_EV_OVERLAY`), and
`world_to_screen` (sporekit/gamehooks.cpp) reads the viewer's transform as it is at that moment. The labels read
every second frame during a turn jump by 150-300 px between two reads and settle where the picture ends, and the
picture of a turn frame shows the camera of the read before it: the viewer at Present is at least one camera step
ahead of the picture just drawn. Both files are the same in v0.7.11.2 (d3d9hook.cpp only gained the overlay shapes).

Guesses, not checked:
* That the frame of the report was shot inside a turn of the guest's bot (its log has no time for the drags).
* How large it is for a player: the bots' drag turns the camera by tens of degrees a frame; a mouse turns it a few
  degrees a frame, which would be some tens of px of tag sliding during the turn (more at a low frame rate). No
  player has reported it.
* Where exactly the game writes the viewer between drawing and Present (cCameraManager::Update after the draw, or a
  second camera pass); the fix depends on it.

Ruled out: BUG-0939 `find.placed`, BUG-0920 / BUG-0928 (one creature for everyone), the portraits: none of them is
on the path, and the old build shows the same.

## Fix
Open, for 0.7.13 (no change in the mod for 0.7.12). Direction: project the labels with the camera the picture was
drawn with: keep the viewer's transform and view window of the moment the game draws the frame (a copy taken in the
game's draw, or at the end of cCameraManager::Update of that frame if the draw follows it) and let
`world_to_screen` / `pfind::project` use that copy in the overlay pass. All overlay labels go through the same
function (name tags, arrows, owner tags, civ and tribe tags, the Space marks), so one fix covers them. Old way behind
a switch.

## Verification
`python3 tools/lab.py run tag_camera --lane N` (extended suite, 80 s): today a measurement (RESULT OK unless a
fault), its last line counts the turn shots by kind. With the fix: `coop_tag_camera.py --limit 0` must PASS (every
turn shot `over` or `none` because the partner is outside the view) and FAIL with the switch off; today it would
FAIL (4 of 12 shots off or over empty ground in lab 20261111-194717). The body is found by the red of the creature
fixture's creature, so the test is for that fixture.

## History
- 2026-11-11: opened.
- 2026-11-11: analysed on lane 6 (fix/w47-tag-empty): not a 0.7.12 regression, the labels are projected with a camera ahead of the picture while the camera turns (also in v0.7.11.2); test tag_camera; stays open for 0.7.13.
