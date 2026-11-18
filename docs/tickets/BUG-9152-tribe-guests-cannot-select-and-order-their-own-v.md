---
id: BUG-9152
title: tribe: guests cannot select and order their own villagers with the game's own controls
status: open
kind: bug
stage: Tribe
reported: 2026-11-12
source: owner 2026-11-12 (the Village page question) and QA
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual two-PC run 2026-11-12)
signature:
docs: docs/re/tribe.md#tribe-guest-native-control
related: BUG-9067 BUG-9020 BUG-9135
switch: tribe.guest_native
---

## Report
Owner (2026-11-12), about the Co-op window's "Village >" page: "why can't they play their village? ... We shouldn't need this button." QA: on a guest's game (the MSI), a villager card click selected nothing and a real right-click on the ground with the own villagers selected (`coop.tribe select_village`) sent no order (cmds_sent 0).

## Root cause
Spore keeps one player tribe per game; on a guest's game it is the host's village, the guest's own village is an NPC tribe there. The terrain cursor's candidate list (cursor +0x164) is the player tribe's mSelectableMembers (+0x354) and Tribe_IsPlayerOwned compares with the player tribe, so the native box / click selection and the right-click orders never reached the guest's own villagers; only the Village page's own buttons did.

## Fix
As civ.cpp does for a guest's nation (h_TerrainCursor, BUG-9067 / BUG-9135): on a guest in Tribe the cursor's candidates are its own village's selectable members (tribe::guest_cursor_candidates, the village cached by guest_tick), and Tribe_IsPlayerOwned is true for that village on its game; the orders go to the host through the existing hooks (h_DoActionForObjects, h_WalkSelected). `coop.fix tribe.guest_native off`: as before.

## Verification
no-fail-run: two-PC manual run, not a lab test. QA 2026-11-12, the MSI as guest (seat 2): before, a real right-click with its villagers selected sent nothing (cmds_sent 0); after: the same click sent the walk (cmds_sent 2) and the host carried out 3 orders, twice; the MSI's cursor candidates read its own village's +0x354 (3 members). Not yet seen live: a real box selection around the guest's villagers (they walked home before the box; the host's idle villagers return to their village), tool, rack, hut and social actions. Open until those pass and the Village page can go.

## History
- 2026-11-12: opened.
