---
id: BUG-0383
title: Creature: bodies of killed NPCs reported out of sync between host and guest (not reproduced)
status: open
kind: desync
stage: Creature
reported: 2026-11-01
source: player report 2026-11-01 (host F9 note at s 1777)
role: both
versions: 0.7.3.3
fixed_in:
branch: fix/w30-npc-walk
commits:
tests: tools/tests/coop_npc_corpse.py (not in a suite)
signature:
docs:
related: BUG-0015, BUG-0339, BUG-0361
---

## Report
A 0.7.3.3 session (2 players, Creature): the host noted "bodies of other creatures are desynced after
killing" (the corpses of killed NPCs in other places or states on the two machines). The logs show nothing
about it; the dead counts of the 10 s summaries matched (host "npcs simulated: N within 60 m (2 dead)", guest
"npcs mirrored: ... (2 dead)") while the players were together. Many host lines "npc N ... is updated at the
planet's centre: herd 00000000, ~505 m from where it was a frame before, dead=1" (BUG-0339's diagnostics):
dead NPCs going back to the game's creature pool (eaten / decayed corpses), and the host then despawns them
for the guest (far from it).

## Root cause
Not reproduced (fix/w30-npc-walk, lanes 4 and 5): `coop_npc_corpse.py` kills 3 NPCs near both players
(`--by host`: the host's avatar hits, `--by guest`: the guest's hit request) and compares the body (the
model's position, AnimatedCreature +0x04, and the object's) on both games 3 s after each death: 0.00 m
apart in 6 of 6 kills, the same death animation F14FAA46 on both (no lab runs, by hand). Not tried: a corpse
eaten on one machine (eating is local; the other machine keeps the body), NPCs killed by other NPCs, kills
far from the guest that come into its view later, a guest at a low frame rate.

## Fix
None.

## Verification
no-fail-run: not reproduced.

## History
- 2026-11-01: opened.
