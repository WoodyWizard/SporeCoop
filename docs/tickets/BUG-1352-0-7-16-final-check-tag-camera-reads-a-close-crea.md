---
id: BUG-1352
title: 0.7.16 final check: tag_camera reads a close creature's shaded back as no body, a tag over its middle as off
status: fixed
kind: test
stage: Creature
reported: 2026-11-20
source: lab 20261120-041304
role: guest
versions: 0.7.16 (unreleased)
fixed_in: 0.7.16
branch: fix/w60-dna
commits: 48dfc9fa
tests: tag_camera (twin tag_camera_drawncam_off), tag_height
signature:
docs:
related: BUG-0948, BUG-1200
---

## Report
rel/0.7.16 worktree out/lab/20261120-041304 tag_camera FAIL: turn 10 "off (+8 px); tag (742, 701), body x 664", the only bad shot of 12 (--limit 0).

## Root cause
A test bug in the picture measurement: in frames/tag_camera/tagcam_turn_10.jpg the close creature is seen from behind,
its back in shade (r 70-120, g/b 20-40); measure()'s red() needs r > 140, the body split into two lit runs 632-694 and
794-856 px, 100 px apart, more than the 80 px merge; the tag at x 742 stands over the middle of the back and was
compared with the left run. The tag is drawn right; not related to 0.7.16's own species (the fixture's red species).
The same tree passed in 20261119-214823 (8 over, 0 off).

## Fix
coop_tag_camera.py measure(): body() (the species' red in its own shade: r > 45, g, b < 70, r > 2 max(g, b); the brown
ground has r/g ~1.1) bridges a gap of any width when every 4th column of the gap holds at least 3 body pixels; the
criterion (the tag within the body +- 30 px) unchanged. Offline over the 245 tagged tag_camera frames of every
worktree (also the BUG-0948 frames of w47/w48) only this frame changes (body 632-694 -> 632-856).

## Verification
Was waiting for the lab run. PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run tag_camera tag_height --lane 2 --lane 3 --retry 0 --no-build`; FAIL (twin,
new suite line): `SPORE_DETECTOR=20 python3 tools/lab.py run tag_camera_drawncam_off --lane 2 --retry 0 --no-build`. Then `tools/frames_review.py <run> --test
tag_camera` (the tag over its creature in tagcam_turn_*).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): tag_camera PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643; tag_height PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-071312 (its FAIL in 20261123-023643 is its at-rest height check, 1.32 m, not the camera read of this ticket); the twin tag_camera_drawncam_off FAIL in 20261120-130608. Closed.

## History
- 2026-11-20: opened.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
