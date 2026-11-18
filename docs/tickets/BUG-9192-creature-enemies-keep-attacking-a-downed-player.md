---
id: BUG-9192
title: Creature: enemies keep attacking a downed player instead of going back to their own behaviour
status: fixed
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: host
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 64c3afed
tests: tools/qa_win (to run: a downed player with an enemy on it)
signature:
docs:
related: BUG-0512 BUG-9149
switch: npc.downed_spared
---

## Report
Owner live test 2026-11-13 20:31-20:55 (their own PC hosting Vandh.spo in Creature; two test guests from the MSI joined late: MSIGuest with a creature, MSIJoiner without one, editor first). "When a player is down, the enemies continue attacking the downed player rather than initiating their normal behavior."

## Root cause
Observed in code: creature_rescue::protect makes a downed body take no damage, but nothing touches the attackers: their target (+0xE7C) stays on the body.

## Fix
npc.downed_spared (on), npc.cpp downed_spared: on the host every creature the game updates whose target is a downed body (the host's avatar while down, a partner's puppet whose state has PF_DOWNED) drops it with SetTarget (vt 0x84, (null, 1, 0)) and its stimuli from that body (as BUG-0512 for the pack).

## Verification
no-fail-run: not run again live yet (the owner tests the build on their install).

## History
- 2026-11-13: opened.
