---
id: BUG-9130
title: Creature: Ctrl+Scroll changes the FoV steps but the picture does not change
status: fixed
kind: bug
stage: Creature
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev ("FoV ... isn't functioning or adding camera FOV")
role: local
versions: 0.7.3.6-lineages41-dev and later (at least)
fixed_in: 0.7.8-lineages54-dev
branch: feat/lineages-47
commits:
tests: QA Samuth.spo host 9: `coop.fov 0` / `coop.fov 6`, screenshots
signature: coop.fov "steps=6 vanilla=44.56 effective=62.56", the frame identical to steps=0
docs: -
related: BUG-9099
switch: creature.fov_update
---

## Findings
* The player's own log: Ctrl+Scroll reaches the mod (fov: steps=1 .. 6).
* QA: steps 6 -> effective lens 62.56 (vanilla 44.56) inside fov.cpp's begin_view, but the screenshots at steps 0 and
  6 are the same frame (same trees and horizon). The projection set through the view's perspective setter during
  BeginUpdate is not the one the scene is drawn with (another pass or a later camera update rebuilds it). Needs the
  camera pipeline (cCameraManager::Update, the follow camera 0xB03AF999's own field of view) looked at.

## Cause (2026-11-11)
Lineages 36 moved the wide lens from after App::cCameraManager::Update (Lineages 30, it widened the picture; BUG-9099
was that it then lasted the whole session) to a scope around App::cViewer::BeginUpdate / EndUpdate, to keep the
native zoom travel and the click ray out of it. Set there, the perspective is not what the scene is drawn with: the
picture never changed again (36..53).

## Fix
fov.cpp `creature.fov_update`: after the camera manager's update the viewer gets the wider perspective
(SetPerspectiveProjection, keeps aspect and clipping planes); before the next update the camera gets its exact previous
view window back (only if nothing changed it), so the camera's own zoom and follow math stay native and nothing
compounds. The wide window stays between draws, so GetCameraToPoint casts clicks through it (the ray correction of
36..53 is skipped). The mode reset of BUG-9099 stays.

## Test (2026-11-11, two PCs)
TITAN hosts a copy of the owner's Vandh.spo (profile DuoQAJiffHost, Creature), a QA guest and the MSI join. On the MSI
(`coop.fov N`, `shot`): mean pixel difference to the normal frame: normal again 7.4, steps 6 with the fix 33.7 (visibly
wider, fov_compare.png), steps 6 with the switch off 7.1 (the bug). Native zoom on the MSI (`wheel` -3, +3, +3) with
steps 6: vanilla 44.56 / effective 62.56 throughout (no compounding); steps 0 back to 44.56.
