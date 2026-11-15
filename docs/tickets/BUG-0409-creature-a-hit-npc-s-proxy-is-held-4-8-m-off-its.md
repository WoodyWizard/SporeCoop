---
id: BUG-0409
title: Creature: a hit NPC's proxy is held 4.8 m off its host's path for 1.1 s (the game undoes the placing after a 3.3 m push), then catches up
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-01
source: lab
role: guest
versions: 0.7.3.3
fixed_in:
branch: fix/w46-k3-bodies
commits: db4b6090, 462fc0f0
tests: body_trail_hits (coop_body_trail.py --stage creature --hits), npc_walk, npc_walk_player
signature: coop.npctrack: a walked proxy (how 3) in the flee phase after `coop.npc hosthit` moves 3.3-3.8 m in one frame (before the mod's driving), then stands 4.8 m off for 1.1 s, or flies off at 8-11 m/s with the fall animation and is teleported back 2.4-4 m
docs: docs/re/creature.md#creature-hit-throw
related: BUG-0361, BUG-0305, BUG-0407, BUG-0675
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
No game fix kept (2026-11-06, fix/w45-remote-bodies-2). The described mechanism is gone in 0.7.9: observed, the guest's game never throws a living proxy (locomotion mode +0x2B0 = 3 on a guest proxy only after its death, the corpse being held at the host's place), and since npc.air (BUG-0550, 0.7.7), walk_place's keep_lost and drive.lead (BUG-0425) no proxy is held off its host NPC after a hit. A host-side throw flag (npc.host_knockback: NF_THROWN, states every frame while thrown, the proxy placed on the path, the lead up to 45 m/s) was written, measured and removed: no clear gain (mean error in the first second after a push hit 0.59-0.90 m on vs 0.94 m off), and its first version made Tribe animals jump. Kept: the probe (coop.npctrack columns lm = +0x2B0, gr = +0x137), the test body_trail_hits and the symbols (SporeCoop::CreatureBase_TakeDamage, SporeCoop::Creature_LocoReset).

## Verification
no-fail-run: the reported hold is not reproduced on 0.7.10 code. body_trail_hits (11 hits per run, every other one with a push): clean network out/lab/20261107-103912 2 of 2 (lane 6; longest over 2 m after a hit at most 0.2 s); at 30 fps over SPORE_NET=player out/lab/20261107-103414 1 of 2 and out/lab/20261107-112617-2 1 of 2: the two failures are a push hit's copy standing while the host's NPC is thrown, then stepping after it (over 2 m 0.88-0.96 s, max 6.0-7.4 m), with no throw of the proxy by the guest's game (guest lm 0): not this ticket's hold, now BUG-0675. Earlier: 13 runs of fix/w45-remote-bodies-2 (below).

## History
- 2026-11-01: opened.
- 2026-11-05 (fix/w44-height): not a height problem (the guest game's knockback on a proxy); not looked at.
- 2026-11-06 (fix/w45-remote-bodies, M1): a different mechanism from the class's lag (BUG-0425): the guest's game
  reacts to the hit on the proxy (a knockback) and undoes the placing. body_trail_creature hits two NPCs per run
  (`coop.npc hosthit`): in 6 runs with drive.lead no copy over 2 m off the host's body for more than 0.07 s; npc_walk
  (flee phase) jumps of ~1 m in 14 ms frames after the hit were seen with and without the lead (out/lab/20261106-021152
  with, the 0.7.8 baseline npc_walk_player 20261106-011339 without): the knockback stays; not touched, open.
- 2026-11-06 (fix/w45-remote-bodies-2, M2): probe of the locomotion mode on both games: the guest's game does not throw living proxies; the remaining error after a push is the host's throw seen one state period late, never held; a host-side throw flag tried and removed (no gain); closed not-reproduced with the runs.
- 2026-11-07 (fix/w46-k3-bodies, K3): one more attempt on the current build with the low fps / bad network variant: the hold is not seen (the guest's game never throws a living proxy); the variant's failures are another mechanism, opened as BUG-0675. Stays not-reproduced, closed with these runs.
