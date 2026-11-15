---
id: BUG-0871
title: Feature idea: a depth handle in the creature editor that pushes details and weapons into the body
status: open
kind: feature
stage: Creature
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages42-dev, BUG-9103 (editor_depth.cpp)
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
Player request in the fork: a depth handle like the vehicle / outfitter editors have, so details and weapons can
be pushed into the body or pulled out in the creature editor. Spore has none for creature parts.

## Root cause
Not a bug. RE (fork): Spore's depth handle is the "nudge" deformation handle (kind fnv("nudge") 0x503283AA); only the
Adventure outfitter setup A56567F7 names a nudge tuning file (0x10CB4B28!0x3C3BA77F), which is in no game package;
creature part models have no such deformation. Adding a nudge definition in IModelWorld::GetDeformationHandles
(vt 0xE8, 0x745000) crashed the handle build. See docs/re/creations.md#creations-fork-editor-depth.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages lineages42-dev, BUG-9103, mods/coop/editor_depth.cpp); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* Move a part: write EditorRigblock +0x48 (position) and its model's offset (model +0x0C), set EditorModel +0x51 = 1
  (changed) and call cEditor::CommitEditHistory 0x586410 (thiscall(bool true, 0), ret 8): Save / Replace are
  offered, Undo restores it, the bem keeps the position, reopening keeps it. cEditor* at Editors::cEditor::ptr
  0x15E4EF0 (runtime 0x1C24EF0); editor+0xD4 selected rigblock; rigblock +0x1C/+0x20 part instance / group.
* Only creature editors (EditorModel type 0x9EA3031A); no handle on the spine (modelIsVertebra), limbs (ball / socket
  connector, capability limb), hands, feet. The mod draws a small lit cube on the overlay, 0.18 x part scale out
  from the nearest spine segment; colours from the editor handle tuning (default (0.67, 0.75, 1.2), rollover
  (0.54, 0.81, 0.54), held: the power handle orange (0.93, 0.48, 0.16)); hidden while a modal dialog is up
  (window manager +0x82C). The press is swallowed so the part stays selected; drag moves along the out line.
  Limits from body depth seen first (out at most 0.3 x it and 0.2 x part scale, in no closer than 0.25 x it);
  the mirrored twin moves mirrored; release commits one undo step. Switches editor.depth, editor.depth_cube.
* Not in plan-0.7.12 (an editor feature, local only; creations already travel by the creation stream). Vanilla
  Spore loads the result.

## How to test
One instance, Creature editor: select an ear, drag the handle, Save New; the saved bem holds the new position, the
twin is mirrored, Undo restores it, reopen keeps it; a partner receives the creation with the same bem hash.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
