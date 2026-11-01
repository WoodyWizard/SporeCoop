---
id: BUG-0383
title: Creature: bodies of killed NPCs reported out of sync between host and guest (one cause found: bodies of NPCs that died before the guest came near were never shown)
status: open
kind: desync
stage: Creature
reported: 2026-11-01
source: player report 2026-11-01 (host F9 note at s 1777)
role: both
versions: 0.7.3.3
fixed_in:
branch: fix/w30-npc-walk, feat/w33-tests-creature
commits:
tests: tools/tests/coop_npc_corpse.py (suite corpse_positions, corpse_positions_player)
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
feat/w33-tests-creature: a guest skipped every NPC whose first state it received was dead
(`npc.cpp` guest tick: `if (!px.c && (s.flags & NF_DEAD)) continue;`, meant for a body the guest's own game had
removed). So the guest never showed the body of an NPC killed while it was more than 110 m away (the host sends
NPCs within 110 m of a guest), killed by another NPC before its proxy was made, or lying there when it joined;
the host's player saw the body, the guest saw nothing. Now the proxy of such a body is made and plays the death
where the host's body lies, unless the guest's game had made it once already (`Proxy::ever`: a body the guest's
game removed stays gone). Switch: `coop.fix npc.dead_proxy off` (0.7.3.3 behaviour).

## Verification
`coop_npc_corpse.py` (suite `corpse_positions`) kills standing, guest-killed, running (6 m/s), thrown (a hit with a
push), slope (0.22) NPCs, an NPC killed by a spawned epic, one killed while the guest was 150 m away, and one the
guest clicks to eat; checks at 3 s and 30 s that the guest shows each body dead (the game's own flag of the proxy),
within 1.5 m of the host's body and on the ground on both.
- FAIL with `SPORE_FIX_OFF=npc.dead_proxy`: out/lab/20261101-135546 (feat/w33-tests-creature worktree): the epic's
  victim #26 and the `later` kill #6 have no body on the guest ("proxy not spawned"); every other kind 0.00 m apart.
- PASS with the fix: out/lab/20261101-141938 (corpse_positions), 20261101-142305 (corpse_positions_player: guest at
  30 fps over the `player` network).
- `SPORE_FIX_OFF=npc.play_death` fails too (the guest's proxies of the bodies are not dead in its game): 20261101-145148.
Still not reproduced: a body in another place on the two games (every body of every kind was 0.00 m apart: the guest
puts a killed proxy where the host's body is every frame).

## History
- 2026-11-01: opened.
- 2026-11-01: feat/w33-tests-creature: corpse_positions in the suite; it found the bodies a guest never shows (NPCs
  dead before its proxy was made); fixed behind `npc.dead_proxy`. Positions themselves matched (0.00 m) in every kind.
