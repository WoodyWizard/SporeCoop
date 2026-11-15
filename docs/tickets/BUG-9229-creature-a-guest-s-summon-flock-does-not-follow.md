---
id: BUG-9229
title: Creature: a guest's Summon Flock does not follow the guest
status: open
kind: bug
stage: creature
reported: 2026-11-15
source: owner play test 2026-11-15
role: guest
versions: 0.7.14.1-lineages58-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Owner play test 2026-11-15 (host Pandize on TITAN's Steam Spore, guest BigTexas over the internet, 58-dev): "Summon Flock isn't following BigTexas when they call it." Host log 17:44:07: "guest_pack: the flock summoned 4 for player 1".

## Root cause
Not known yet: the host made the flock for the guest; why it does not follow is to be seen live (the game's summon event may keep leading it).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: open, not reproduced yet

## History
- 2026-11-15: opened.
