---
id: BUG-0878
title: Civ: while the host is away a guest's capture shows "won" on its screen but is never made (suspected)
status: open
kind: bug
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0775
---

## Report
While the host is in an editor, a menu or a load (local play), a guest that besieges a city sees its capture progress fill and the win, but the city is not captured; the host's state undoes it when it is back.

## Root cause
Inferred, confidence low-medium: in local play `cosmetic_hit` returns false (`mods/coop/civ.cpp:1085`), so the guest's game accumulates capture progress and shows the result, while Capture itself is blocked on a guest (`civ.cpp:1685`).

## Fix idea
In local play either keep the capture meter from finishing (hold it below the threshold with a notice "the capture waits for the host") or queue the capture and send it to the host when it is back.

## How to test
Host `coop.civ away 1` (or a held editor), the guest besieges an NPC city to the end; then the host returns: the city's owner must be the same on both games and match what the guest was shown.

## History
- 2026-11-10: opened from the Tribe / Civ / Space code audit for the 0.7.13 plan.
