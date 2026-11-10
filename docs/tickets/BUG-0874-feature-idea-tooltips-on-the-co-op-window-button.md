---
id: BUG-0874
title: Feature idea: tooltips on the co-op window buttons in Spore's own style
status: open
kind: feature
stage: all
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages42-dev, BUG-9107 (coop_ui.cpp)
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
The co-op window's buttons have no explanation; the fork shows short tooltips in Spore's native style after a
0.8 s rest.

## Root cause
Not a bug, a UI feature.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages lineages42-dev, BUG-9107, mods/coop/coop_ui.cpp); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* The tooltip background is Spore's own drawable class UTFWin::StdDrawable (ctor 0x988350, object 0x7C bytes, images
  at +0x10), with a 9-slice image data/ui/lineages_tooltip.png; shown after the mouse rests 0.8 s on a button,
  hidden on move / click. Text per button in English and Russian.
* Overlap: plan-0.7.12 F1 (co-op window 2.0); add the tooltips to the new window.

## How to test
UI test by real mouse moves: rest 1 s over Host, the tooltip window is visible with the text; move away, hidden;
no tooltip before 0.8 s.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
