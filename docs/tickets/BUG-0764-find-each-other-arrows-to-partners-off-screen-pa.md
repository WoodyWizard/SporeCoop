---
id: BUG-0764
title: Find each other: arrows to partners off screen, partners on the minimap, player colours
status: fixed
kind: feature
stage: All
reported: 2026-11-08
source: Discord suggestions 2026-10-31..11-04 (Sterben, M0ng0r: arrows to partners, partners on the map), docs/plan-0.7.12.md F2
role: all
versions: 0.7.11
fixed_in: 0.7.12
branch: feat/w46-partner-arrows
commits: 7b08921b, 7995da26, 76477f2c, a0457e7c, 11d74ec1, e7411236, 9f1f1759, eb8aaaa9, 44a73ce2, 6dda6833
tests: find_partners, find_partners_tribe, find_partners_cell, find_partners_civ, find_partners_space
signature:
docs: docs/08-ui.md#ui-find-each-other, docs/re/creature.md#creature-minimap, docs/re/tribe.md#tribe-minimap
related: BUG-0762, docs/plan-0.7.12.md F2
---

## Report
Discord suggestions 2026-10-31..11-04: "Arrow or something to indicate which player is where" (players lose each other,
in the Cell stage most of all) and "We can see each other on map" (partners on the minimap in their own colour). Plan
0.7.12 F2 (approved 2026-11-08): (a) an arrow at the screen edge to every partner off screen, in the player's colour,
with the name and the distance, in every stage with an avatar view, off in Rivals and switchable (F3); (b) partners on
the minimap of Creature and Tribe; (c) a player colour, assigned (4 distinct) or picked in the lobby.

## Root cause
Not a bug: nothing showed where a partner was once it left the screen (only "look here" pings and the knocked-out cell
arrow), the minimap showed no partner, and players had no colour (name tags green, owner tags blue / gold).

## Fix
* Colours: the host's table (find_core.h Colors, MSG_COLORS 67), a palette of 8, the first free of 4 in join order, a
  pick (`pfind::request_color`, the API for F1's lobby; `coop.find color <name>`) refused when another player has it,
  stored in coop.ini `[coop] color`. Name tags, owner tags, galaxy tags, chat lines in the colour.
* Arrows: SporeKit `overlay_tris` (filled triangles in the overlay pass, render states restored by a state block);
  `pfind::draw_arrows` from coop.cpp `find_targets` (Cell / Creature / Space the puppet, Tribe the chief, Civ the
  pointer else the first city); behind the camera the direction comes from the farthest point in front on the way.
* Minimap: the game's cMinimap blips (RE docs/re/creature.md#creature-minimap), MoveBlip / AddBlip / RemoveBlip every
  0.25 s outside the frame, the colour as the blip's tint (icon flags 0x4, a small pointed mark).
* Settings `all.partner_arrows` (live, off in Rivals) and `all.partner_map` (live). Switches: `find.arrows`,
  `find.colors`, `find.map` (off = the 0.7.11 behaviour).

## Verification
* PASS: lab 20261108-161238 (find_partners, find_partners_tribe, _cell, _civ, _space; lanes 9/10), also
  20261108-155841 and 20261108-160151.
* FAIL with the switches off (find_partners, lane 9): `SPORE_FIX_OFF=find.arrows` lab 20261108-160354 (no arrow pixels),
  `find.colors` lab 20261108-160500 (the games show different colours), `find.map` lab 20261108-160615 (no blip on
  either minimap).
* Looks: `find_icons` lab 20261108-161041 (the blip icons by flags; 0x4 chosen). Screenshots of each stage for the
  owner's review in the branch's worktree out/f2-shots/.

## History
- 2026-11-08: opened.
- 2026-11-08: implemented on feat/w46-partner-arrows (F2 of 0.7.12).
- 2026-11-11: released in 0.7.12.
