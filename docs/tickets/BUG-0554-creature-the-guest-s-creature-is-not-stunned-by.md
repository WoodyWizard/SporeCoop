---
id: BUG-0554
title: Creature: the guest's creature is not stunned by an NPC's charge
status: fixed
kind: bug
stage: Creature
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-guest-player
commits: 7a97b869, a8845587, 998985ba
tests: coop_creature_stun.py
signature:
docs: docs/re/creature.md#creature-player-rules
related: BUG-0418
---

## Report
The guest: "I don't get stunned by other creatures' charge". A charge from an NPC hits the guest but does not stun it as it does in the host's own game.
Reports: SporeCoop-report-2026-11-04 175959 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
* Observed (Ghidra + lane 9): the stun of a charge is not a hit. The ability hit `SporeCoop::CreatureAbilityHit` 0xC20390
  applies the damage through TakeDamage (type 2, forwarded to the guest as MSG_DAMAGE) and then the ability's effects:
  kind 1 calls the victim's vt 0x94 `SporeCoop::CreatureStun` 0xC02C70 (charge 0x1F: chance 1.0, 1.5 s), which posts
  stimulus lo 0x1000 with the seconds in the victim's behaviour tree (the stars, the stagger). In the host's game the
  victim is the guest's puppet (no AI): the stun never left the host's game. The guest's charge on a proxy (no AI)
  likewise never stunned the host's NPC.
* Observed: TakeDamage type 3 is the charm (mCharmDamage / mCharmTime +0x169C), not the stun.

## Fix
`player_rules.cpp` (`creature.stun_relay`): a hook on CreatureStun. Host: on a partner's puppet -> MSG_PLAYER_RULE (63)
kind 0 {player, seconds} to that partner, whose game calls CreatureStun on its own avatar (the game's avatar scaling:
1.5 s -> 3.0 s); guest: on a proxy -> kind 1 {NPC id, seconds} to the host, whose game stuns the NPC.
`coop.fix creature.stun_relay off`: the stun stays on the puppet / proxy.

## Verification
coop_creature_stun.py (suite creature_stun): the host's NPC with a charge aims at the guest's puppet and plays its
charge (`coop.rules charge`: SetTarget + the game's PlayAbility; the hit and the stun are the game's); the guest's own
avatar must hold stimulus 0x1000; then the guest's CreatureStun on a proxy must stun the host's NPC.
* FAIL with `SPORE_FIX_OFF=creature.stun_relay`: out/lab/20261104-210333, out/lab/20261104-214243 (the charge hit the puppet, the guest never stunned; the NPC not stunned).
* PASS: out/lab/20261104-210216, out/lab/20261104-210851, out/lab/20261104-213617 (2 runs), out/lab/20261104-220621 (the guest's avatar stunned 3.0 s, counting down; the NPC stunned).
* A guest sends the stun of a proxy only inside its own avatar's ability hit (CreatureAttackTarget's attacker): a
  partner's puppet plays abilities in the guest's game too (got_npc on the host with no charge of the guest, out/lab/20261104-212957).
* Not verified: an NPC choosing its charge by itself (the test plays it); the stars on the guest's screen were seen in a screenshot (lane 9), not asserted.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), cause inferred from the logs and npc.cpp, not reproduced in the lab.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): fixed: CreatureStun relayed both ways (creature.stun_relay).
- 2026-11-05: released in 0.7.7.
