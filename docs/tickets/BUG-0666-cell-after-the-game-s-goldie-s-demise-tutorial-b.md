---
id: BUG-0666
title: Cell: after the game's Goldie's demise tutorial both avatars keep their collect locks (+0x178): no part token can be collected (lab; players unknown)
status: wontfix
kind: bug
stage: Cell
reported: 2026-11-07
source: fix/w46-part-token lab probes 2026-11-07, cell_part_token FAILs 20261106-231404, 20261107-004042
role: both
versions: 0.7.9-dev
fixed_in:
branch: fix/w46-k8-open
commits:
tests: cell_goldie, cell_part_token
signature:
docs: docs/re/cell.md#cell-goldie-button, docs/known-issues.md#issue-cell-goldie-locks
related: BUG-0316, BUG-0633
---

## Report
cell_part_token failed on rel/0.7.9 (out/lab/20261106-231404, 20261107-004042 in the rel/0.7.9 worktree): neither
game collected any part token. Probes (lane 3, 2026-11-07): from the moment the shared food crosses the Goldie window
both avatars have the lock bytes +0x16C, +0x178, +0x17B, +0x17C set (and +0x170 = Goldie's index), animation +0x18C 0,
for good (30+ s, also after own meals and swimming); 0xE7B630 refuses every token while +0x178 is set. Also with the
session left (one game alone with the mod loaded).

## Root cause
Observed (Ghidra): AddFoodPoints 0xE7CE10 calls 0xE79720 once per game when S+0x68 == 0, the food BEFORE the add is
121-150, at most 4 parts are unlocked and the difficulty is not hard: it destroys the cells around the avatar (5 and
15 m), makes two cells (G+0x516C Goldie, and its eater with +0x22C = Goldie), sets S+0x68, stores an event of type
0x24 (15 s) at G+0x5194 / G+0x5198 and plays the cinematic "goldies_demise". The event's handler 0xE794F0 stops the
cinematic, destroys Goldie with its loot (the first part token) and sets the avatar's +0x170 = Goldie. Runtime: the
event fires after its 15 s, the locks stay. A meal crossing food 200 from 190 does not start it (outside the window).
Not known: what clears the locks in a normal game (the cinematic's end? player input?), whether players hit it
(the report's host had collected parts, "3/6"). The test's `coop.pool food 40` steps cross the window.

## Root cause (2026-11-08, fix/w46-k8-open)
Not a mod bug: the lab's tests never pressed the cinematic's button. Observed (Ghidra, a static RE pass; runtime lab
20261108-010939):
* The locks are per frame: 0xE59E80 (from SimUpdate 0xE7F3A0, before the event pass) memsets +0x16C..+0x1AF of every
  pool cell each frame; an active event sets them again. Event 0x24's per-frame action 0xE5F160 (0xE7BA30 case 0x23)
  sets the avatar's +0x170 = Goldie, +0x178, +0x17C, +0x17B, +0x16C = 1. The event outlives its 15 s timer while
  G+0x5198 names it (0xE7E6C0 keeps applying it), so the lab dump (locks set, +0x18C 0) is that action, every frame.
* The cinematic goldies_demise (script in Spore_Game.package) ends in `cell_button: wait -forButton`. Its button
  (0xAD94A0, control 0x287259F6) posts message 0x445F729; the Cell game's listener 0xE7F960 ends the event at
  G+0x5198 (0xE7E130 -> 0xE794F0: Goldie's loot, the first part token) and the next frame's memset frees the avatar.
  Esc does the same (0xE7F630 -> 0xE7F5A0, skipped). Earlier note "event 0x24 fires after 15 s": CORRECTION, only its
  timer runs out; 0xE794F0 runs at the button or Esc.
* Co-op does not break it: cell_goldie (both games play the tutorial from the shared food, then each player presses
  Esc in its own game, guest first): each game's avatar is free within 6 s of its own Esc, the other keeps its own
  tutorial until its player ends it (lab 20261108-010939).

## Fix
None in the mod (the game waits for its player, as in a single-player game). Tests: cell_part_token marks the
tutorial played (`coop.pool mate nogoldie`); cell_goldie checks a player's own input ends it in a session. Earlier:
Test only so far: cell_part_token marks the tutorial played on both games (`coop.pool mate nogoldie`, S+0x68 = 1).
The game-side question (does a player's game clear the locks after goldies_demise; does co-op break it) is open.

## Verification
no-fail-run: not a mod bug (the game's cinematic waits for its player's button or Esc); cell_goldie PASS
out/lab/20261108-010939 (both avatars locked 14 s after the tutorial, each freed by its own player's Esc).

## History
- 2026-11-07: opened.
- 2026-11-08: closed (fix/w46-k8-open): the cinematic waits for its button; a player's Esc frees the avatar in a session (cell_goldie).
