---
id: BUG-9229
title: Creature: a guest's Summon Flock does not follow the guest
status: fixed
kind: bug
stage: creature
reported: 2026-11-15
source: owner play test 2026-11-15
role: guest
versions: 0.7.14.1-lineages58-dev
fixed_in: 0.7.14.1-lineages60-dev
branch: feat/lineages-47
commits: e1ce9e69
tests: manual Windows QA: the MSI, two games, coop.gpack summon 1
signature:
docs:
related:
---

## Report
Owner play test 2026-11-15 (host Pandize on TITAN's Steam Spore, guest BigTexas over the internet, 58-dev): "Summon Flock isn't following BigTexas when they call it." Host log 17:44:07: "guest_pack: the flock summoned 4 for player 1".

## Root cause
Inferred from the code and docs/re/creature.md#creature-c2-audit-0713: guest_pack::summon started the summon event with the host's avatar as its leader; the event's update steers its members for 2-3 minutes and at its end tells them to follow the leader (the host). Upstream's drama::summon_for (BUG-0736) runs the event with the guest's puppet as the avatar and was not used by guest_pack.

## Fix
- guest_pack::summon starts the event through drama::summon_for (the guest's puppet as the avatar for its start and every update). Switch creature.guest_flock_leader.

## Verification
PASS (the MSI, two games): "summon event for a partner's creature: started"; the flock stayed 3-6 m from the guest for 3 minutes (coop.gpack).
no-fail-run: the old behaviour (the flock drifting to the host) was not filmed; Windows QA by hand.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed (the guest leads its flock).
