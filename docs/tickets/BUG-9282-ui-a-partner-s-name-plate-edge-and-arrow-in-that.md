---
id: BUG-9282
title: UI: a partner's name plate edge and arrow in that player's colour
status: fixed
kind: feature
stage: all
reported: 2026-11-19
source: owner 2026-11-19 (7D A)
role: all
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits:
tests: tools/qa_win (live TITAN QA 8+9 2026-11-19, switch off and on)
signature:
docs: docs/ROADMAP.md (7D)
related:
---

## Report
Decision 7D A (owner 2026-11-19: "7DA - make sure the white border is replaced, not a new one added."): a partner's
name plate shows that player's colour on its edge and its arrow (mockup images/mockup_7D_plate_colour.png).

## Root cause
Feature. The plate (decision 3E C) had a light edge (0xC8EBF0F5) and the HUD's blue arrow for everybody.

## Fix
coop.cpp, the partner marker: the plate's own edge is drawn in the player's colour (find.h color_of, the F2 colour),
2 px instead of the 1 px light one (partner_marker::plate's new width; the same ring, so no second border), and the
arrow is filled with that colour (red while the partner is down, as before). Switch `ui.plate_colour`.

## Verification
no-fail-run: a feature. TITAN 2026-11-19, QA 8 and QA 9 in one party on Samuth (Creature): switch off, the white edge
and the HUD's blue arrow; on, the edge in QA 9's colour (blue for that player) and no white edge left
(images/7DA_plate_colour_off_on.png). Not seen yet: a player with another colour (orange, green) on the plate.

## History
- 2026-11-19: built on feat/lineages-alpha.
