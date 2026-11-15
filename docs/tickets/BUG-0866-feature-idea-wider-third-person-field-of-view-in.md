---
id: BUG-0866
title: Feature idea: wider third-person field of view in Creature and Adventure (Ctrl+Scroll)
status: open
kind: feature
stage: Creature
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages30..41-dev, BUG-9099 (fov.cpp)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Players want to see more around a big creature in third person. The fork widens the field of view of the
Creature / Adventure follow camera by up to 18 degrees with Ctrl+Scroll.

## Root cause
Not a bug, a new feature.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages lineages30..41-dev, BUG-9099, mods/coop/fov.cpp); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* Hooks: App::cViewer::BeginUpdate 0x7C5080 and EndUpdate 0x7C3CE0 (GOG 2024), and cViewer::GetCameraToPoint (picking).
  In BeginUpdate, when the active camera id (cCameraManager::GetActiveCameraID 0x7C61F0 on the manager of
  cGameModeManager::GetCameraManager 0x967D90) is the follow camera 0xB03AF999 and the mode is Creature or
  Adventure, take the viewer's perspective camera (viewer+0x170, camera+0x6C == 1), remember its view window
  (camera+0x5C / +0x60) and set a wider one with cViewer::SetViewWindow 0x7C4BD0; restore it in EndUpdate. So
  native zoom and camera logic between draws see the stock lens.
* GetCameraToPoint: recompute the click ray with the wide lens (camera viewport +0x78/+0x7C/+0x80/+0x84), else world
  clicks miss (this broke mating clicks, BUG-0177 follow-up).
* Steps 0..6, +3 degrees each; Ctrl+Scroll needs MK_CONTROL and GetAsyncKeyState(VK_CONTROL); feed line on each
  change; reset when leaving Creature / Adventure (editors keep it); `coop.fov reset|<0-6>`.
* Not in plan-0.7.12. Lens is local only (no network).

## How to test
One instance, Creature: Ctrl+Scroll 3 notches, check camera+0x5C/+0x60 during the draw is wider and the stock
value outside it; a real click on an NPC selects that NPC (picking matches); leaving to the editor and back keeps
it, to Tribe resets it.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
