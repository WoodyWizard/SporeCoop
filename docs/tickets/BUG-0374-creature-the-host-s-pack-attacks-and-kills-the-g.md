---
id: BUG-0374
title: Creature: the host's pack attacks and kills the guest after the guest hits a pack member (retaliation on the puppet, hits forwarded)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-01
source: player reports 2026-11-01 (host mark 13:30:57)
role: guest
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w30-creature-hp
commits: d62e750
tests: tools/tests/coop_creature_party.py (suite creature_party)
signature:
docs: docs/re/creature.md#creature-max-hit-points
related: BUG-0016, BUG-0345
---

## Report
Host mark (0.7.3.3, Creature): "my own group can attack other players for some reason and kill them"
(the host's pack attacks the guest; the guest's own game went 13 -> 4 -> 0/14 in the half minute
before the mark, a pack rescue in between).

## Root cause
Observed on lane 9 (the trigger in the players' session is inferred: the logs have no per-hit line).
The attacker is the host's pack member **in the host's game** (the guest's mirrors run no AI): a
guest's hit on a host pack member (MSG_DAMAGE_REQ) is applied with the guest's puppet as attacker;
the game's TakeDamage posts stimulus 0x400 with the puppet as sender to the victim's AI, the pack
member attacks the puppet for good (a 0.5 hit, then a hit every ~1.5 s for minutes), and
`npc::damage_filter` forwarded every NPC hit on a puppet to its player as MSG_DAMAGE: the pack is
not exempt. The partner is a valid target because a puppet is just a creature of another species
(its own herd) in the host's game; nothing marks it as the host's ally. Not triggered on lane 9: the
pack never attacked the puppet by itself (30 s next to it), nor after the host's avatar "attacked" it
(the attack action message 0xD335362C, 3 times). BUG-0345 is an own-creations design ticket, not
related to the relation between the players.

## Fix
`npc.party_peace` (creature_hp.cpp, called from npc.cpp): on the host a partner's hit on a pack
member (`+0xB58 & 0x100`) or the avatar's herd (family) is dropped (no friendly fire, as between
players), and a hit on a puppet by the host's pack or family is not forwarded. Off: 0.7.3.3.

## Verification
`coop_creature_party.py`: the host recruits a wild NPC (`coop.npc recruit`), the guest stands next to
the host and hits the pack member once; for 15 s the guest's own game health never drops (sampled
every 0.2 s), the pack member's AI holds no stimulus naming the guest's puppet, the pack member's
health is unchanged.
* FAIL with `SPORE_FIX_OFF=npc.party_peace`: out/lab/20261101-080817-fix-w30-creature-hp (7 health drops on the guest in 15 s, 1 stimulus from the puppet in the pack member's AI, pack member 6.0 -> 5.5).
* PASS: out/lab/20261101-080343-fix-w30-creature-hp (with the other two tests), out/lab/20261101-081255-fix-w30-creature-hp (warm, with the regression tests creature, team, creature_desync, npc_run_hp, puppet_walk: 8/8).

## History
- 2026-11-01: opened from the host's report (0.7.3.3); reproduced and fixed on lane 9 (fix/w30-creature-hp).
- 2026-11-02: released in 0.7.3.4.
- 2026-11-03: a related report on 0.7.3.6: the pack attacks the guest when the HOST targets or attacks it (another path, BUG-0512).
