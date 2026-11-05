---
id: BUG-0409
title: Creature: a hit NPC's proxy is held 4.8 m off its host's path for 1.1 s (the game undoes the placing after a 3.3 m push), then catches up
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: lab
role: guest
versions: 0.7.3.3
fixed_in:
branch: fix/w34-walk-stable
commits:
tests: npc_walk, npc_walk_fps30, npc_walk_player
signature: coop.npctrack: a walked proxy (how 3) in the flee phase after `coop.npc hosthit` moves 3.3-3.8 m in one frame (before the mod's driving), then stands 4.8 m off for 1.1 s, or flies off at 8-11 m/s with the fall animation and is teleported back 2.4-4 m
docs:
related: BUG-0361, BUG-0305, BUG-0407
---

## Report
npc_walk on fix/w33-null-call 793e207 (= main's driving), lab 20261101-135017 (lane 3): "no jump of a moving proxy (1)"
(a 0.76 m step of proxy #2 in a 30 ms frame after a 62 ms one, at t 47.29 s). Earlier runs of the same driving with
the same pattern: 20261101-121954 npc_walk_fps30#1 (2.36 m), npc_walk_player#1 (3.99 m), 20261101-123326
npc_walk_fps30#1 (2.68 m, walk phase, see below). Found while classifying the walk tests' failures (BUG-0407).

## Root cause
Observed (coop.npctrack rows, guest; `d` = drawn before the driving, `p` = after it):
* 20261101-135017 #2 (hit at 45.08 s): from 45.6 s the proxy's game speed grows to 6 m/s while the host NPC slows to
  2 m/s; each frame the game moves it 0.12 m and walk_place pulls it back by (speed + catch_up) x dt = 0.07 m, its error
  grows 0.65 -> 1.49 m; at 46.137 s the game moves it 3.3 m in one frame (off the screen). From then on every frame's
  placing (0.05-0.1 m towards the path) is undone by the game (drawn position unchanged, d-prev p = p-d) for 1.1 s:
  the proxy stands 4.8 m off its host NPC on the guest's screen while the NPC runs at 1-9 m/s, until the NPC's path
  comes back to it; then a 0.76 m catch-up step (1.3x the NPC's 9.2 m/s: the test's jump limit used the run's top
  speed 4 m/s, BUG-0407 changed that).
* 20261101-121954 npc_walk_fps30#1 #4 (flee): the proxy plays the fall animation and the game moves it away at
  8-11 m/s (0.25-0.36 m per frame) against the placing; at 2.67 m off it is teleported (2.36 m drawn step).
* 20261101-121954 npc_walk_player#1 #1 (flee): the game moves the proxy 3.8 m in one frame, the driving teleports it
  back (4 m drawn step on the screen).
* 20261101-123326 npc_walk_fps30#1 #3 (walk phase, standing NPC, drive::step): the game moves the proxy 0.6 m per
  frame while the nudge pulls it back 0.6 m; at 2.68 m off a stall teleport.
Inferred: the guest's game applies its own reaction to the hit (a knockback / push) to the proxy, and the
placing is undone while it lasts; the host's NPC is not moved that way. Not fixed on fix/w34-walk-stable.

## Fix
not fixed yet.

## Verification
no-fail-run: open

## History
- 2026-11-01: opened.
- 2026-11-05 (fix/w44-height): not a height problem (the guest game's knockback on a proxy); not looked at.
