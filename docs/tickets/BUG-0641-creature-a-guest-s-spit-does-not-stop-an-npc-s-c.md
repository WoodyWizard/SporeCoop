---
id: BUG-0641
title: Creature: a guest's spit does not stop an NPC's charge, an NPC's spit does not stop the guest's charge, some NPC strikes do not hit the guest
status: open
kind: bug
stage: Creature
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-06, M0ng0r-11
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: extend coop_creature_stun.py: an NPC charges, the guest spits at it (a proxy): assert the host's NPC charge
     stops; the guest charges and the host's NPC spits at the puppet: assert the guest's own avatar stops; count the
     NPC strikes that hit the puppet on the host against the hits the guest's game applies. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
