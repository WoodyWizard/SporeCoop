---
id: BUG-0900
title: Space: terrain sculpting and planet colouring done with no partner on the planet never reach the partners
status: fixed
kind: desync
stage: Space
reported: 2026-11-10
source: fix/w47-b2-space (BUG-0763, 2026-11-10)
role: both
versions: 0.7.12 and earlier
fixed_in: unreleased
branch: fix/w47-colony
commits: 82b16f99
tests: space_sculpt_away
signature:
docs: docs/re/space.md#space-sculpt-colour
related: BUG-0763, BUG-0883
---

## Report
Found while fixing BUG-0763 (not run). The sculpting / colouring relay (spacecoop.cpp K_SCULPT / K_COLOUR) reaches only
partners whose ship is on the same planet at the moment of the hit. A player who sculpts or recolours a planet alone,
or a partner who lands later or rejoins, sees the old terrain and colours for good (the same class as BUG-0883 for the
atmosphere / temperature).

Reproduce: space_free, the guest lands on 01460035, the host stays in the system; the guest uses the first sculpting
tool (tab W-226, slot (1108, 966)) and the first colour (tab W-186, slot (1121, 966)) on the ground
(coop_space_sculpt_tool.py's clicks); the host lands: coop.ground at the guest's `coop.space sculpt` `last=` spot and
`coop.space sculpt` `colours` differ.

## Root cause
* Observed (code): apply_sculpt / apply_colour return when the receiver is not on that planet.
* Inferred: the colours live in the terrain's prop list (sphere +0x28, props 0x3A23F9A / 0x536250C / 0x536250D) and the
  sculpts in the sphere's terrain effect list +0x798 (docs/re/space.md#space-sculpt-colour); where the game keeps them
  for a planet that is not loaded (the planet record or a planet file) is not known. A fix needs that first: then the
  host keeps a per-planet list of the hits (effect id + transform, last colour triple) and sends it to a partner who
  lands (or applies it to the planet's stored data).

* Observed (code, fix/w47-colony): the sender already sends every hit of its own tool (sculpt_place needs only a
  session and a planet; the host relays a guest's), so the hits reach every game; the receiver dropped them.
* Observed (lab 20261110-202317, switch off): the guest's ship off the planet during the host's two raises and a
  colour; after the guest's landing its terrain at the spot 14.33 m from the host's and its colours (0, 0, 0) against
  (0, 11, 0).

## Fix
Decided to fix now (cheap: no planet-record data needed). spacecoop.cpp: a K_SCULPT / K_COLOUR for a planet this game
is not on (or came to less than 5 s ago) is kept with its planet and world (session state; at most 50 sculpts and 64
colours a planet, the oldest go; 1024 in all) and repeated in the order it came 5 s after this game's ship is on that
planet (terrain_hold_tick), with the game's own calls as for a partner on the planet. Counters `held= held_done=` in
`coop.space sculpt`. Twin switch `coop.fix space.sculpt_hold off` (dropped, 0.7.11).
Limits (written in docs/re/space.md#space-city-data): a player who joins or rejoins after the hits does not get them
(the hold lives in the running games, not in the planet record); whether the game keeps sculpts and colours across a
leave and a landing, and in the save, is not traced (0.7.13 if a report shows it).

## Verification
`coop_space_sculpt_tool.py --away` (suite `space_sculpt_away`): both land on 01460035, the guest takes off, the host
raises the ground twice and colours the planet with real clicks, the guest lands again: its terrain around the spot
(6 points) and its colours must be the host's.
* FAIL with `SPORE_FIX_OFF=space.sculpt_hold`: lab 20261110-202317 (14.33 m apart, colours (0, 0, 0) / (0, 11, 0)).
* PASS: lab 20261110-201800 (0.00 m, colours (0, 11, 0) on both; the guest `held=0/3 held_done=3`).

## History
- 2026-11-10: opened.
- 2026-11-10: fixed on fix/w47-colony (a partner's hits for another planet kept and done on landing; switch space.sculpt_hold; test space_sculpt_away).
