---
id: BUG-9121
title: the arrow to an off-screen partner points down instead of at the partner
status: fixed
kind: bug
stage: all
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev (Cell)
role: all
versions: 0.7.8-lineages49-dev
fixed_in: 0.7.8-lineages50-dev
branch: feat/lineages-47
commits:
tests: QA Cellfive.spo host 9 + guest 8, `coop.pool tp 60 25` on the guest, host screenshot with ui.offscreen_arrow off / on
signature: -
docs: -
related: BUG-0160
switch: ui.offscreen_arrow cell.offscreen_arrow
---

## Cause
SporeKit's world_to_screen returns 0 both when the point is behind the camera (the point is not written) and when
it is projected outside 1.2x the view (the point is written). coop.cpp's partner arrow and map marks and
cellsync's knocked-out-partner arrow treated every 0 as "behind" and pointed the arrow down.

## Fix
The point is preset to NaN; a written point is "in front" and gives the direction even off the screen.

## Verification
Guest 60 m right / 25 m up of the host: off = arrow at the bottom pointing down ("Guest 60 m"); on = arrow at the
top right pointing up-right ("Guest 62 m").
