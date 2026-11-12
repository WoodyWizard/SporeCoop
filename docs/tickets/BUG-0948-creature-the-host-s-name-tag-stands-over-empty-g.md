---
id: BUG-0948
title: Creature: the host's name tag stands over empty grass on the guest's screen, no creature under it (play_creature mid, seen once)
status: open
kind: bug
stage: Creature
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-151535
role: guest
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
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
Not known. Two readings of the frame, neither checked:
* the tag is at the host's real place and the guest's puppet of the host is not drawn there (the red creature at the
  left is then a family animal of the host's nest, the same species), or
* the red creature at the left is the puppet, at a stale or wrong place, and the tag follows the reported state
  instead of the puppet (the reverse of BUG-0907 in Tribe).
BUG-0929's height (1.5-2 m over a low creature) does not explain it: at 7 m that is under 100 px and the ground
there is empty.

## Fix
Open.

## Verification
Open. How to reproduce: `tools/lab.py run play_creature` (seed 924813650 in this run) and look at the mid frame; or
two games in Creature, the guest with its own tall creature, the host 5-8 m in front of it in high flowers, then on
the guest compare `coop.find` (the tag row and its position) with `coop.status` (the puppet's position) and a
screenshot. The check a test lacks: same_view / find_partners compare the tag's screen position with the projected
position of the puppet the guest's game draws (within some px), not only with the reported state.

## History
- 2026-11-11: opened.
