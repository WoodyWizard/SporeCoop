---
id: BUG-9102
title: Graphic Settings: the Frame Rate Cap cannot be changed, and after a change Settings no longer opens
status: fixed
kind: bug
stage: all
reported: 2026-11-09
source: player report (Lineages 40-dev, Steam, 2560x1440 borderless)
role: local
versions: 0.7.3.6-lineages36-dev to 0.7.3.6-lineages40-dev
fixed_in: 0.7.3.6-lineages41-dev
branch: fix/fov-stuck-wide-lens
commits:
tests: QA pause-menu flow twice without Backspace (coop.fix ui.fps_replace on/off), see Verification
signature: lineages-graphics.ini unchanged after editing the cap; Settings button inert after an accepted change
docs: docs/06-testing.md
related: BUG-9057
---

## Report

"The Frame Rate Cap in the options menu doesn't let me change it." The
player's `lineages-graphics.ini` (display=144,1) had not changed since the
morning although they tried.

## Findings

- The field holds at most three digits. With "144" every typed digit was
  ignored until Backspace removed one, and a real Backspace can reach Spore's
  menu navigation. Synthetic QA input (posted WM_KEYDOWN) does not show that
  second part, so earlier QA passed.
- After an accepted change, Accept re-applied `window.borderless` even when
  only the cap changed. Afterwards the pause menu's Settings button no longer
  opened Graphic Settings until a restart (QA: accept with no change reopens;
  accept with a change does not).

## Fix

- After a click into the field, the first digit replaces the value; Delete
  clears it (`coop.fix ui.fps_replace`, off = before).
- Accept applies only what changed: `frame.target` for a new cap,
  `window.borderless` for a new window mode.

## Verification

QA copy `out/release36/native`, profile LineagesDiff41QA, Creature save,
640x480 secondary monitor, 10 FPS, muted; pause menu -> Settings -> click the
cap -> type (no Backspace) -> Accept, twice in one session:
- fix off: 144 stays 144 both times (the report).
- fix on: 144 -> 60 -> 120, Settings reopened for the second edit. 0 faults.
- Same flow from the galaxy menu Options: passes (90 saved).
Not covered: a real keyboard Backspace (would need global input injection).

## History
- 2026-11-09: reported by the player; fixed and released in 0.7.3.6-lineages41-dev.
