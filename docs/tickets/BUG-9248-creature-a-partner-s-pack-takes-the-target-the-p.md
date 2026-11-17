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
tests: manual Windows QA with tools/qa_win/qa.py (TITAN, Samuth.spo, host 9 + guest 8)
signature:
docs: docs/re/creature.md#creature-summon-pool
related: BUG-9129, BUG-9233, BUG-9245, BUG-9247, BUG-9249, BUG-0512, BUG-0735
---

## Report
Owner question 2026-11-16: "Does it actually attack and help impress?" TITAN QA (Samuth.spo, after BUG-9245 / BUG-9247): the partner's flock (3 summoned creatures, simulated by the game, following the partner) got the target when the partner hit a wild creature (`assists` +3, `coop.gpack` target = the victim) but stayed 1-2 m from the partner for 25 s while the victim came within 20 m and went away again; nobody ran at it.

## Root cause
Observed (TITAN QA 2026-11-16 22:00-22:08, Samuth.spo, test creatures from `coop.npc spawn 06806931` next to the host's nest):
* The game's pack order: cPosseSimulator_Relay (Steam 0xC42E40, read live) case 0x10 (the avatar's animal target) and case 0x400 (the avatar's attacker) give each pack member that is not allied with the target (0xAFEA30(member, target, 6)) and passes PosseMemberCanDefend the same call: CreatureCommandStimulus(member, 0x10, 0, 0x20030030, 0x40040000, FLT_MAX, 0x100000, 0, 0, 0, target, 0).
* The host's own flock fights: the host bit a test creature once (18 -> 14.6) and its 4 summoned members (1.2-2.5 m from it) took it to 0 within 10 s with no further bite.
* The partner's flock does not, with SetTarget (63-dev) or with that same command (creature.guest_pack_attack): 3 summoned members and 1 recruit got it (`commands=` counted, 2 declined by PosseMemberCanDefend), stayed 4-7 m away and the creature's health did not move for 35 s. The pack flag alone (+0xB58 |= 0x100) changed nothing.
* One of the partner's members added to the game's pack with cPosseSimulator_AddMember (flag 0x100 and the pack list) obeyed the next command: it went from 7.9 to 1.7 m and the creature fell 25.1 -> 14.1 in 35 s while the other members (not in the pack) stood by.
So the order works only for a creature in the game's pack (AddMember); the partner's flock is kept out of it on purpose (BUG-9233: it filled the host's pack, size +0x18, 3 at that brain level).
Not looked at: which part of AddMember the behaviour tree checks (the pack list, the posse simulator's own member entries).

## Fix
Partly: creature.guest_pack_attack gives the partner's members the game's own order (with PosseMemberCanDefend first, as the relay does); it has effect once a member is in the game's pack. How the partner's pack joins the game's pack without filling the host's is decision 5C (docs/ROADMAP.md).

## Verification
FAIL (63-dev SetTarget, switch off, 21:54): the partner hit a wild 067C6931 35 m away; its health stayed 71.5; the 5 members never closed in and all died to its herd within 35 s.
Not passed yet: with the order alone (switch on) the members still stood by (22:03, 22:05); one member in the game's pack fought (22:08).
no-fail-run: Windows QA by hand (no lab on this PC).

## History
- 2026-11-16: opened during the owner-requested pack test.
- 2026-11-16 (later): cause found (only members of the game's pack obey the pack's attack order); the order itself built (creature.guest_pack_attack); joining the pack is decision 5C.
