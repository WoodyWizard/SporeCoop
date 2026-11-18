---
id: BUG-9145
title: editor depth cube highlights but cannot be dragged in some camera orientations
status: fixed
kind: bug
stage: Creature
reported: 2026-11-12
source: owner report 2026-11-12
role: all
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: c1ec1834
tests: tests/editor_depth_axis_tests.cpp
signature:
docs: mods/coop/editor_depth_axis.h
related: BUG-9103 BUG-9116 BUG-9121
switch: editor.depth_axis
---

## Report
Owner: "The Square nudge tool in the creature creator looks great, but sometimes you can't grab it? It highlights, but the click and drag doesn't respond when in certain orientations."

## Root cause
Code reading (inferred, not reproduced live): editor_depth.cpp input() measured the drag axis with the point one world unit out along the part's line (about 300 px at the usual zoom). When the part faces the camera or the screen's edge that point is off the screen or behind the camera; world_to_screen returns 0 for both (BUG-9121), the press returned false and went to the editor: the cube is lit (the hover test passed) but does not drag. A line pointing at the camera (under 1 px per unit) was refused the same way.

## Fix
editor_depth_axis::drag_axis: the axis from a 0.05-unit step (the cube's own measure), off-screen points allowed; a line shorter than 3 px on screen (it looks at the camera) drags up / down at the cube's scale (up = out). `coop.fix editor.depth_axis off`: the old refusal; `coop.editor_depth` (status) counts head-on drags and refused presses.

## Verification
no-fail-run: unit test only (tests/editor_depth_axis_tests.cpp); not yet tried in the editor.

## History
- 2026-11-12: opened.
