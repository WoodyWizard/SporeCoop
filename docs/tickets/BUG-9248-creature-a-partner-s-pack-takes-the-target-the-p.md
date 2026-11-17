---
id: BUG-9248
title: Creature: a partner's pack takes the target the partner hits but does not go and fight it
status: open
kind: bug
stage: Creature
reported: 2026-11-16
source: QA TITAN 2026-11-16
role: host
versions: 0.7.14.1-lineages63-dev
fixed_in:
branch:
commits:
tests: manual Windows QA (TITAN, Samuth.spo, host 9 + guest 8)
signature:
docs: docs/re/creature.md#creature-summon-pool
related: BUG-9129, BUG-9245, BUG-9247, BUG-0512
---

## Report
Owner question 2026-11-16: "Does it actually attack and help impress?" TITAN QA (Samuth.spo, after BUG-9245 / BUG-9247): the partner's flock (3 summoned creatures, simulated by the game, following the partner) got the target when the partner hit a wild creature (`assists` +3, `coop.gpack` target = the victim) but stayed 1-2 m from the partner for 25 s while the victim came within 20 m and went away again; nobody ran at it.

## Root cause
Observed: guest_pack's assist calls the member's SetTarget (vt 0x84) with the victim's combatant, and the game keeps it: +0xE7C of all 3 members read the victim's combatant (victim + 0x5A8) a second later. The members still did not move to it.
Inferred, not checked: a summoned creature's behaviour (the "follow" its summon posted, the posse's follow) wins over a target; the host's own pack is sent to fight by the posse simulator (cPosseSimulator), and the partner's flock is out of the host's posse (BUG-9233).
Seen once: when a wild herd attacked the partner itself, `defends` went 8 -> 96 and all 4 members died within 12 s near the fight; whether they hit back was not seen (the damage log kept only other hits).

## Fix
Not fixed. Next step: read how the posse simulator orders the host's pack to attack (and the summon's follow), then order the partner's pack the same way.

## Verification
Not verified (open).

## History
- 2026-11-16: opened during the owner-requested pack test.
