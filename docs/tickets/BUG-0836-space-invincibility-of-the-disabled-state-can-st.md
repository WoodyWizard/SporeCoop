---
id: BUG-0836
title: Space: invincibility of the disabled state can stay on a new ship of the player (suspected)
status: not-reproduced
kind: bug
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch: fix/w48-space
commits: 3f6fb767 52f5971d
tests: space_down_editor
signature:
docs: docs/re/space.md#space-s1-0713
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
Not reproduced: no trip inside one Space world gives the player a new UFO object, so down_tick's write always lands on
the ship the state was set on. Kept as a guard (it costs nothing, and a later game path that rebuilds the ship would
hit it): `space.down_ship_only` writes the invincible byte only while the own ship is the recorded one
(down_ship_here: pointer, object serial, world epoch), and an end of the state on another ship clears the byte on the
current own ship too. Switch `coop.fix space.down_ship_only off` (0.7.12). `coop.space downstate` now shows `ship=
serial= down_ship= down_serial= inv= other_ship= other_cleared=`. No CHANGELOG line (nothing a player could see).

## Verification
`coop_space_down_editor.py` (suite `space_down_editor`): a probe of the trips a player makes in one Space world, the
own ship's pointer and the game's object serial before and after each:
* landing and taking off, a jump to another star and back, the galaxy view and back into the home system, the captain
  screen's "Enter Outfitter" (its parent is hidden in the lab saves, the click at its place opens nothing), and a death
  of the ship (the disabled state with nobody to repair it) with the respawn at the colony ("We've built you a new
  ship"): the same object every time (0ECE8FB8 / serial 212, lab 20261112-035944; 10E37D10 / 212 without the death,
  lab 20261112-035324 with the switch off).
* No spaceship editor can be entered from Space in these saves (the outfitter edits the captain and did not open); an
  adventure from Space was not tried (it loads its own world and ship), nor the outfitter in a save where it opens.
* The test now checks that after the disabled state ended (with the death and the respawn) the own ship takes damage
  (`hit 20`, invincible 0).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: not reproduced on fix/w48-space (the player's UFO stays the same object over every trip of one world, a respawn included); the guard space.down_ship_only kept.
