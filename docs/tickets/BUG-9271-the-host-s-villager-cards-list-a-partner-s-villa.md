---
id: BUG-9271
title: The host's villager cards list a partner's village member (Tribe_IsPlayerOwned answers true for it)
status: open
kind: bug
stage: Tribe
reported: 2026-11-20
source: found in the port of BUG-9268 (lane D, 2026-11-20)
role: host
versions: 0.7.15.5 (feat/lineages-tribe)
fixed_in:
branch: feat/lineages-tribe
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-own-villages
related: BUG-9268
---

## Report
With a partner's own village in Tribe (BUG-9268), the host's villager cards (the column on the right of the HUD) showed
a purple card of the partner's species among the host's three red ones (screenshot on lane D). Clicking it selects
nothing (h_SetSelected refuses another player's member).

## Root cause
Inferred: the hook on Tribe_IsPlayerOwned (`tribe.guest_ai`, Lineages BUG-9020) answers true for a partner's village on
the host so that its members take orders as player members (raids); the cards are built from the player-owned
citizens. Which caller builds the cards is not known (not traced).

## Fix
Not fixed. Candidate: answer true only for the callers of the citizen's AI (scoped by return address, as h_NounData), or
leave the cards' caller the game's answer.

## Verification
Repro: lane D, the graduation to Tribe with a guest on its own creature; on the host, look at the villager cards.

## History
- 2026-11-20: opened.
