---
id: BUG-0779
title: Screen tearing / unstable picture with the mod active since 0.7.3.4 (frame pacing, need info, suspected)
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: Discord 2026-11-09 Screen tearing
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/02-modkit.md
related: BUG-0653, BUG-0667, BUG-0672
---

## Report
Discord 2026-11-09 "Screen tearing" (host, Creature stage, video and report zip not readable by us): "the game behaves
like this since multiplayer is on, not in single player; the same when the 30 fps lock is removed; 0.7.3.3 did not
have it, the picture was stable". Need info: the report zip, whether vsync is on in the game's options.

## Root cause
Suspected, confidence low:
* Observed (code): SporeKit never changes the swap interval or the device: h_CreateDevice / h_Reset only log
  (sporekit/d3d9hook.cpp, no PresentationInterval, SwapEffect, timeBeginPeriod or Sleep in the frame path).
* Inferred: the mod's whole frame work runs in `on_frame_begin_present` before the game's own Present
  (`d3d9hook.cpp:410`, `:426`). Long or uneven ticks in a session move Present against the vblank: with vsync,
  30 -> 20 / 15 fps judder; without it, a wandering tear line. Candidates for unevenness: the co-op UI refresh
  outside the frame every 0.25-1 s (`mods/coop/coop_ui.cpp:1834`), the game-loop frame path with an overlay, and
  puppets moving in steps (which a player may call tearing).
* SporeKit changes in frames since v0.7.3.3: BUG-0653 (presentlost), BUG-0667 (frameunwind), BUG-0672 (outside tasks
  wait for the frame's end), report shots.

## Fix idea
First measure: a Present interval statistic in SporeKit (per second: mean, p95, max interval between Presents, and
the time the frame work takes before o_Present) in the perf line and the report. If the mod's work is the cause, move
the heavy parts after Present or spread them (outside tasks, not every frame).

## How to test
Lab: the Creature fixture, host alone vs host with a guest, 2 min each, read the new Present interval statistic.
Pass condition: p95 of the interval in the session within 10 % of solo and no extra intervals > 50 ms. Run it on
v0.7.3.3 too to see whether the difference is new.

## History
- 2026-11-10: opened.
