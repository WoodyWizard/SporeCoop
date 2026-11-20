---
id: BUG-9271
title: The host's villager cards list a partner's village member (Tribe_IsPlayerOwned answers true for it)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-20
source: found in the port of BUG-9268 (lane D, 2026-11-20)
role: host
versions: 0.7.15.5 (feat/lineages-tribe)
fixed_in: unreleased
branch: feat/lineages-tribe
commits: 5e2418fa 1716e16c fafe5a96
tests: tools/tests/coop_tribe_seats.py (suite tribe_seats)
signature: a villager card of the partner species on the host
docs: docs/re/tribe.md#tribe-own-villages
related: BUG-9268
---

## Report
With a partner's own village in Tribe (BUG-9268), the host's villager cards (the column on the right of the HUD) showed
the partner's villagers: after the founding the partner's chief and two villagers among the host's chief and two, and a
card for every birth in the partner's village. Clicking one selects nothing (h_SetSelected refuses another player's
member).

## Root cause
Observed (lane D, memory of the running game and runtime): the Tribe UI makes a villager card when a citizen joins a
village, at 0xCCC6E8: `mov edx,[ebx]; call [edx+0x58]` (the citizen spatial's IsPlayerOwned, Citizen_IsPlayerOwned
0xC24590, which asks the citizen's tribe +0x1014), then on true the card. The hook on Tribe_IsPlayerOwned (`tribe.guest_ai`,
Lineages BUG-9020) answers true for a partner's village on the host so that its members take orders as player members.
Ruled out with a deny list of callers: the birth's other askers 0xC0D049, 0xC275C8, 0xC26EC7, 0xC8ECBD and the UI-range
askers 0xE8C584, 0xE93ED9, 0xEA540F, 0xE10649, 0xDBD2EF, 0xCDF4E7 (a card came each time); denying 0xCCC6EA alone: none.

## Fix
Citizen_IsPlayerOwned hooked: for the card caller (return 0xCCC6EA, `SporeCoop::TribeUI_CardOwnedReturn`) a partner's
villager gets the game's own answer (false); every other caller keeps the mod's (the AI's raids). Switch
`coop.fix tribe.host_cards off`: a card for each, as before. `coop.tribe villages deny <va>...` stays for tests.

## Verification
no-fail-run: not run in the Linux lab (no lane for this batch). Twin on Windows lane D, 2026-11-20, a host with a
partner village, a birth there through `coop.tribe villages baby 1`: with `tribe.host_cards off` a new baby card on the
host (screenshot e1); on, the next birth made none and `host cards refused 1` (screenshot e2). The cards after a load with
the fix: the host's only (e0). coop_tribe_seats.py checks the counter only (the cards are UI).

## History
- 2026-11-20: opened.
- 2026-11-20: fixed on feat/lineages-tribe (5e2418fa).
