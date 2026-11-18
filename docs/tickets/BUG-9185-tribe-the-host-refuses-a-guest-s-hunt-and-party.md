---
id: BUG-9185
title: Tribe: the host refuses a guest's Hunt and Party right-click orders
status: fixed
kind: bug
stage: tribe
reported: 2026-11-13
source: test
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: d2c9fe7a
tests: tools/qa_win (manual, TITAN host + the MSI guest, msi_input.ps1, 2026-11-13)
signature:
docs:
related: BUG-9032 BUG-9152
switch: tribe.hunt_party_orders
---

## Report
Found in the 3A A check (2026-11-13, a fresh copy of the owner's Jiff save, TITAN host, the MSI as the guest commanding seat 1, real clicks on the MSI): the guest's right-click on its own fire pit sent the game's Party order (action 12) and the host counted it rejected (`rejected=1`), the villager did nothing.

## Root cause
Observed: tribe.cpp checks a guest's order against a list of the game's own orders (gather, attack, raid, equip, heal, eat, gift, perform, walk). Party (12, the fire pit 0x18C8F0C) and Hunt (10, an animal, noun 0x18EB45E) were not on it.

## Fix
tribe.hunt_party_orders (on): Party on a fire pit and Hunt on a living animal (not a villager) are accepted, checked like Eat (the host finds the object by type within 8 m of the guest's position). Off: both refused.

## Verification
no-fail-run: manual, not a lab run. Party, the switch off on the host: the MSI sent action 12 on its fire pit, the host "rejected=1", nothing happened (FAIL). On: "cmds_done=1", "last order: from=2 ... action=12 ... -> 1AFC79C0", the host's villager walked to the fire pit and ran its behaviour (bt 045BC494) (PASS). Hunt: not hit by a real click (the animals were at the screen edge under trees at night; four right-clicks became walks, which the host ran). In the same run: Gather (action 2 on a fruit tree) and Eat (action 0 on the food mat) passed on the host; a food gift to the host's hut sent no order (the hut was under the host's name tag and arrow).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on feat/lineages-47 (decision 3A A check).
