---
id: BUG-0865
title: Feature idea: frame rate cap 1-360 and borderless window in Spore's Graphic Settings
status: open
kind: feature
stage: all
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages36-dev, BUG-9102 (sporekit focus.cpp, coop_ui.cpp)
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
Players with high refresh monitors ask for a frame rate above Spore's ~30 fps cap (or a lower one for laptops)
and a borderless window. The fork adds both to Spore's own Graphic Settings panel and to the setup.

## Root cause
Not a bug. We have only the console `frame.limit <ms>` (game config +0xB0, frame ms) and the ini
`[sporekit] frame_ms`.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages lineages36..41-dev, BUG-9102; sporekit/focus.cpp, d3d9hook.cpp, commands.cpp, sdk/include/lineages_graphics.h, mods/coop/coop_ui.cpp refresh_graphics_options); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* Settings file `mods/lineages-graphics.ini` [graphics] display=<fps 0..360>,<borderless 0|1> (0 = game default).
* FPS: `set_frame_target(fps)` sets the game's frame ms (+0xB0) to 0 and paces in `pace_frame_target()` at the start
  of frame_common: wait the rest of 1/fps with a high resolution waitable timer
  (CreateWaitableTimerExW flag 0x2, fallback a plain timer or Sleep). Console `frame.target <1-360>|auto`.
* Borderless: window style without WS_OVERLAPPEDWINDOW, SetWindowPos to the monitor rect (SWP_FRAMECHANGED),
  restore from the saved WINDOWPLACEMENT. Only in windowed mode (D3D9 Reset / CreateDevice tell
  `borderless_video_mode(pp->Windowed)`). `maintain_borderless()` every 10 frames re-applies it outside the frame when
  Spore restored the borders or resized the window (startup, graphics reset). Console `window.borderless on|off`.
* UI: two rows added to Spore's Graphic Settings (window 0x043DE590, graphic panel 0x0437A968, Accept 0x043B7A18,
  Defaults 0x05CACA50, controls 0xC00F0110..0xC00F0113, font taken from 0x0695B438). BUG-9102 lessons: the first
  digit after a click replaces the value, Delete clears it (a real Backspace can reach Spore's menu navigation);
  Accept applies only what changed (re-applying borderless on every Accept broke reopening Settings).
* Setup: fields for both, written to the same file. Not in plan-0.7.12; a small separate package.

## How to test
One instance: `frame.target 120` with vsync off, measured fps 115..121; `auto` back to the game's value; borderless
on: window rect == monitor rect, style has no caption; after a forced video reset it is borderless again within
1 s. UI test by real clicks: type 90 in the field, Accept, the file holds 90, Settings opens again.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
