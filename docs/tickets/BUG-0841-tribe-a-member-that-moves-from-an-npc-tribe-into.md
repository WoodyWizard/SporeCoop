---
id: BUG-0841
title: Tribe: a member that moves from an NPC tribe into the player tribe keeps owner 0xFE and nobody can select it (suspected)
status: open
kind: bug
stage: Tribe
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Suspected from the code: a tribe member that joins the players' tribe from a village stays unselectable for every player, the host included.

## Root cause
Inferred, confidence low: whether the game moves members between tribes (alliance, conquest) is not checked. tribe.cpp:1663-1678 gives a member its owner only when it first appears in g_hm; for a known member only `it->second.tribe` is updated, not the owner. h_SetSelected (:498) then refuses it to everybody. Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
When a member's tribe changes to the player tribe, hand it an owner in turn as for a new member.

## How to test
Conquer or ally a village in Tribe and check `coop.tribe` member list for player-tribe members with owner 254; try to select one on the host: FAIL = owner 254, not selectable; PASS = owned by a player.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
