---
id: BUG-9300
title: Tribe (guest): the member list shows both players' villagers after the own-village swap
status: open
kind: bug
stage: tribe
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9153
---

## Report
Live 67-dev, Tribe: "The Pack list showed both party member's units, rather than just the player's." (the guest's
member cards).

## Root cause
Likely (code + log, not seen on screen yet): the guest's world load builds the member cards while the host's village is
still the player tribe; BUG-9153's swap (`persp_update` / `persp_restore`, tribe.cpp ~2827-2859, SetPlayerTribe) then
makes the own village the player's, which adds its members' cards, and nothing removes the cards of the village that
stopped being the player's (cards leave only through TribeStage_RemoveMemberCard, docs/re/tribe.md#tribe-roster-cards).
The guest swapped and restored many times this session (20:26:40, :50, :53.8, 20:27:01, 20:38:36; restores at 20:26:47,
20:26:53, 20:31:56).

## Fix
Planned: after a swap, take the other village's members off the cards (`body_card_off`, tribe.cpp ~310, out from under
the `tribe.body_card` switch); the same on restore. Switch `tribe.swap_cards`. Test: card count = own members after the
swap and after a save + re-swap (cards() of coop_tribe_roster_cards.py): FAIL (host + own) / PASS.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
