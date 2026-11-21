---
id: BUG-9323
title: Civ: this player's own name tags show the -NoShare- test tag
status: open
kind: bug
stage: Civ
reported: 2026-11-21
source: QA 2026-11-21
role: both
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9222
---

## Report
Live QA 2026-11-21 (TITAN QA copy, Civ, a guest joined as `CivGuest-NoShare-`): the guest's own vehicle's name tag read
`CivGuest-NoShare-` and its capital's `[CivGuest-NoShare-]` (out/qa-shots/civ_guest_own_rings_on_own_vehicle.png).
BUG-9222 hides the tag in the player list and join lines. Partners' tags read the name without it.

## Root cause
Inferred from the code: draw_civ_tags (coop.cpp) uses this game's own name (g_name) as it is for its own units.

## Fix
Not done (only test names carry the tag).

## Verification
no-fail-run: not fixed yet.

## History
- 2026-11-21: opened.
