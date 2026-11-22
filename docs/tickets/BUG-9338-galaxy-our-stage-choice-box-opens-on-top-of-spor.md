---
id: BUG-9338
title: Galaxy: our stage choice box opens on top of Spore's stage loading card after a real Play
status: open
kind: bug
stage: all
reported: 2026-11-22
source: found while testing BUG-9337 (2026-11-22)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/12-server-and-players.md#server-native-stage-choice
related: BUG-9335, BUG-9337
---

## Report
Seen while testing BUG-9337 with its switch off (TITAN, test galaxy, 2026-11-22): a real click on a server planet's
star, then its Play: Spore's stage loading card ("Civilization Stage" with a loading line) fills the screen and our
"Choose a stage" box (BUG-9335) opens on top of it. The card stays until a stage is pressed. Reproduce: `coop.fix
galaxy.native_stage_choice off`, Play on a planet with two eras that this player has not played.

## Root cause
Inferred: the panel's Play switches the menu to its Play state (0xC), which shows the stage card before its Play step
(SporeCoop::GGE_PlayStep) runs; BUG-9335 opens the box from that step, so the card is already up. (The BUG-9335 run
went through `coop.gmenu play`, which never entered state 0xC.)

## Fix
Not fixed. With BUG-9337 on (the default) Play goes to Spore's own Choose A Stage screen and never reaches this path;
the box is only the fallback when that screen cannot open (switch off, symbols missing). A fix would open the box
instead of switching to state 0xC (the same place BUG-9337 switches to state 4).

## Verification
Not fixed.

## History
- 2026-11-22: opened (found while testing BUG-9337).
