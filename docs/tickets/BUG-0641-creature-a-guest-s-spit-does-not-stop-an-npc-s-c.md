---
id: BUG-0641
title: Creature: a guest's spit does not stop an NPC's charge, an NPC's spit does not stop the guest's charge, some NPC strikes do not hit the guest
status: fixed
kind: bug
stage: Creature
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-06, M0ng0r-11
role: guest
versions: 0.7.8
fixed_in: 0.7.10
branch: fix/w46-creature-p0
commits: 234a2d29, b418a0ab
tests: spit_charge (coop_spit_charge.py)
signature:
docs: docs/re/creature.md#creature-nest-follow
related: BUG-0418, BUG-0554, BUG-0555
---

## Report
Discord report 2026-11-06 (0.7.8), the guest (discord-2026-11-06/M0ng0r-06, M0ng0r-11): "sometimes I don't get the hit
from an enemy creature's strike, also I can't counter a charge with spit"; "an enemy creature can't stun my charge with
their spit".

## Root cause
Observed (logs): the guest's 20 "dropped (creature.air_immune)" lines all have amount 0.0 (that filter hid no damage);
the host forwarded 242 NPC hits; no stun line in either log (the BUG-0554 relay does not log).
Inferred: a spit hit interrupting a running charge is not CreatureStun (the only thing `creature.stun_relay`, BUG-0554,
relays) but the ability hit's interrupt on the victim's behaviour, which no relay carries in either direction; "no hit
from a strike" is probably the host's game deciding the reach against the puppet's position (BUG-0418 class:
avatar-only game rules a guest misses).
Related: BUG-0418 (class), BUG-0554 (stun relay, fixed 0.7.7), BUG-0555 (air_immune, fixed 0.7.7).

## Fix
`creature.spit_counter` (player_rules.cpp): the game's rule is in the ability hit 0xC20390 (a spit, type 0x20, stuns
a target whose current ability +0xE8C is a charge, type 0x1F, with the spit's seconds). The charge runs only in its
owner's game, so neither direction held. After the game's hit (CreatureAttackTarget, whatever it returns: the rule
runs before the damage), a spit on the partner's puppet (host) or on a host NPC's mirror (guest) is sent to the
game of the charge (K_SPIT_PLAYER / K_SPIT_NPC), which stuns its own creature when it charges now or its charge hit
within the last 1 s (the spit lands on the mirror while the charge runs there and arrives after the charge's own
hit). `coop.fix creature.spit_counter off`: a spit stops only a charge of the game's own creatures (0.7.9).
The strike half ("sometimes I don't get the hit from an enemy creature's strike") is not a lost hit: the host's log
has 242 lines of NPC hits forwarded to the guest, the guest's 246 lines of hits from the host (Sterben-11, M0ng0r-17;
both rate-limited, every forwarded hit arrived). Inferred: the host's game decides the strike's reach against the
puppet's place there, which lags the guest's own creature (placement): handed to K3 (fix/w46-k3-bodies).

## Verification
coop_spit_charge.py (suite spit_charge, creature fixture). The fixture's creature has neither a spit nor a charge, so
the guest plays its own creature that has both (29021371, chosen in the co-op window, put back at the end).
A: a host NPC charges the guest, the guest's creature spits at its mirror: the host's NPC must hold the stun stimulus
0x1000 in the host's game. B: the guest's creature charges an NPC, a host NPC spits at the guest's puppet: the guest's
own creature must hold 0x1000 in the guest's game. 5 tries each.
* FAIL with `SPORE_FIX_OFF=creature.spit_counter`: out/lab/20261107-080322 (A and B: no stun in 5 tries).
* PASS: out/lab/20261107-080138, out/lab/20261107-080609.
* Before the last change: out/lab/20261107-075402 (sent=0: the hits on a mirror / puppet return false, the old code
  sent only for true), out/lab/20261107-075857 (B passed; A: every spit arrived after the NPC's charge had hit).
* Not verified: an NPC choosing its spit or charge by itself (the test plays them); the 1 s window is a latency
  allowance: a spit that lands up to 1 s after a charge's hit also stuns across the games.

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07 (fix/w46-creature-p0, P0-Creature of 0.7.10): fixed (creature.spit_counter, plus the hit's return value and a 1 s window); FAIL 20261107-080322, PASS 20261107-080138, 20261107-080609. Strike half: no hit lost (host 242 forwarded, guest 246 received); the placement of the puppet goes to K3 (fix/w46-k3-bodies).
- 2026-11-07: released in 0.7.10.
