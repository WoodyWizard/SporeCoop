---
id: BUG-0836
title: Space: invincibility of the disabled state can stay on a new ship of the player (suspected)
status: open
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
A player who was 'disabled' (down) in Space may stay invincible after the state ended, until the next load. Suspected from the code.

## Root cause
Inferred, confidence medium: whether the player's UFO object really changes within a world is not checked. spacecoop.cpp:349 down_tick writes kInvincible = 1 every frame on the current my_ufo(), not on g_down_ship; the end path clears only g_down_ship. A ship editor or adventure trip that makes a new UFO object would keep the flag on. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Write the flag only when my_ufo() is g_down_ship, and clear it on the current ship at the end too.

## How to test
`coop.space disable 1`, then a ship editor trip, wait 20 s: FAIL = `coop.fleet list` shows inv=1 on the own ship after the state ended; PASS = inv=0.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
