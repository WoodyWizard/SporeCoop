---
id: BUG-9154
title: Party food: a share of each delivery for the partners, food gifts between party villages
status: open
kind: feature
stage: tribe
reported: 2026-11-12
source: owner
role: host
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tests/party_food_tests.cpp; tools/qa_win (manual two-PC run 2026-11-12)
signature:
docs: docs/re/tribe.md#tribe-party-food
related: BUG-9153 BUG-9135
switch: tribe.food_share
---

## Report
Owner decision 2P (2026-11-12): "Let's do 25% is split. ... if the 25% split equals 0.2, it'll always be at least 1, even if the total is more than obtained legitimately." And: "I'm excited to gift food to my party members." Before, each village kept all it gathered, and a gift to a party member's village was refused (the host allowed gifts to computer tribes only). Seen on 2026-11-12: an idle host's village starved to 0 food while the partner's held 370.

## Root cause
Design, not a bug. Founder villages have their own native food stores on the host (BUG-9135). A guest's game shows no gift action on a party member's village (no DoActionForObjects from a right or left press on its hut, social stance, MSI 2026-11-12), so the game's own gesture needed the mod's help there.

## Fix
tribe.food_share (on). The host checks every present player's village store 4 times a second against a baseline the mod's own changes move with them; a rise of 0.5 or more is a delivery: the gatherer keeps three quarters, the quarter is split among the other present players' villages, each at least 1 (party_food.h). Gifts: a guest's right press on a party member's hut in the social stance with villagers selected (the game's gesture for other tribes) sends CMD_ACTION 9 on that village; the host's own native Gift on a partner's village is taken in h_DoActionForObjects; party_gift moves the game's gift price (10 here) from the giver's store to theirs and tells both (the host's own line stays on the host).

## Verification
tests/party_food_tests.cpp (the split, the minimum, gifts). no-fail-run for the live part (feature): 2026-11-12, TITAN host + MSI guest: the guest village's deliveries of 9.24 and 5.0 gave the host's village +2.31 and +1.25 (a quarter, over the minimum) while the guest kept 6.93 and 3.75; a real right press on the host's hut on the MSI moved 10 food (host 19.06 -> 29.06, guest 557.09 -> 547.09), "You gave Host's village 10 food." on the MSI. Not seen: the host's own Gift on a partner village (needs clicks on the owner's PC), a delivery small enough for the minimum, 3-4 players.

## History
- 2026-11-12: opened.
