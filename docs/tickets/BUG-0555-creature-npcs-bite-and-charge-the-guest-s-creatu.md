---
id: BUG-0555
title: Creature: NPCs bite and charge the guest's creature while it flies (glides)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-guest-player
commits: 7a97b869, c88909f7, 31bac8b7, 998985ba, 303a1745, aeaa53b2
tests: coop_creature_air.py
signature:
docs: docs/re/creature.md#creature-player-rules
related: BUG-0418
---

## Report
The guest: other creatures can attack it while it is flying (bite, charge, maybe strike). In the game's own rules a flying creature cannot be hit by those.
Reports: SporeCoop-report-2026-11-04 180055 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
* Observed (Ghidra): the game's rule is in one place, the attack node's ability choice 0xD666B0 (BT_AttackTick
  0xD67CD0): against a target with +0x137 == 0 (not grounded), +0x110 & 0x1000 clear, 1 m or more away, only the ranged
  types 0x20 / 0x3D / 0x42 / 0x48 are allowed. The hit frame 0xC1E8B0 (3D reach) and the hit 0xC20390 do not check again.
* Inferred: on the host the puppet is held in the air only while PF_JUMPING arrives, and the game marks it grounded
  again at animation starts (BUG-0532); an attack chosen in such a frame, or before the take-off, lands in the air.
* Observed (lane 9, fix off): an NPC that fought the guest landed a melee hit while the guest's own game had its
  creature off the ground (out/lab/20261104-212457).

## Fix
`player_rules.cpp` (`creature.air_immune`): a hook on CreatureAttackTarget (vt 0xB8, 0xC02B40) on the host: a hit of a
non-ranged ability on a puppet whose player's latest state says airborne (PF_JUMPING), from more than 1 m, is dropped
(no damage, no stun, no push), as the attack node would not have picked it.
`coop.fix creature.air_immune off`: every hit lands.

## Verification
coop_creature_air.py (suite creature_air): the guest provokes a wild NPC (8 s of bites on the ground), then keeps its
creature 1.5 m up for 20 s (up to 3 rounds until the host's NPC struck at the puppet); the guest's damage log counts
the host's melee hits applied while its own avatar was not grounded (+0x137 == 0 at the hit).
* FAIL with `SPORE_FIX_OFF=creature.air_immune`: out/lab/20261104-213320 (8 bites in the air), out/lab/20261104-214119 (8),
  out/lab/20261104-221034 (5), out/lab/20261104-222052 (6).
* PASS: out/lab/20261104-221627 #1 / #2 (7 strikes at the airborne puppet in the host's game, 9 / 11 hits kept from the
  guest, 0 landed), out/lab/20261104-222222 (2 runs).
* Seen on the way: dropping the CreatureAttackTarget call itself left a charging NPC flying on towards the puppet
  (82 m up, sync-check `n apart`, out/lab/20261104-212957): the hit now runs, only its damage / stun are not carried. A
  first version decided on the host only and let a bite through when the host's last state of the guest was on the
  ground (out/lab/20261104-221242 #1): the guest answers the same rule now (MSG_DAMAGE carries the ability type).
* Not verified: a real glide (the fixture creatures have no wings: the test lifts the creature, PF_JUMPING as in a
  jump); the NPC does not always strike at a creature over it (a run with no strike in 60 s, 20261104-221627 #3, before
  the strike count ignored the puppet's grounded byte).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), inferred, not reproduced in the lab.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): fixed: melee hits on an airborne partner dropped by the host (creature.air_immune).
- 2026-11-05: released in 0.7.7.
