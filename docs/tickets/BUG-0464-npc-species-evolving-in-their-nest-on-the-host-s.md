---
id: BUG-0464
title: NPC species evolving in their nest on the host stand still on the guest
status: fixed
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 
fixed_in: 0.7.4
branch: fix/w40-npc-anims
commits: 8ea68520, 92a146f0, d5741801, 271ee35c
tests: npc_anims
signature:
docs: docs/re/creature.md#creature-npc-anims, docs/re/creature.md#creature-bt-anims
related:
---

## Report
Host's mark (s 2333): 'other species is evolving in nest for host but for guest they are standing still.' The nest evolution sequence (animation / cinematic of an NPC species evolving) is not mirrored. Class G2/G4.
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not fixed (fix/w40-creature-acts, 2026-11-03). Observed (lane 9, coop_npc_anims.py, runs 20261103-153901 /
20261103-154027): the guest's proxies show the host NPCs' current animation in 7-9 % of the samples: no NPC
animation reaches the guest, the proxies play only their walk / idle. Herd evolution fields found (+0xF0 generation,
+0xA8 evolved species keys, +0x85 should-evolve). A relay of PlayAnimation / PlayAnimationTo was tried and did not
change the proxies' animations (reverted). See docs/re/creature.md#creature-npc-anims. The test
tools/tests/coop_npc_anims.py (not in a suite) fails on this code and is the check for a fix.

Found on fix/w40-npc-anims (2026-11-03). Observed (Ghidra + runtime): the behaviour tree starts its animations with
SporeCoop::CreaturePlayAnim 0xC122F0, which nothing relayed; a proxy has no AI, so it only showed the idle / walk
choice of its own game (idle refresh 0xC148B0). An animation played on a proxy with the same call stays until it
ends (Update asks the idle refresh only when no main animation is left). docs/re/creature.md#creature-bt-anims.
Inferred: the "evolving in the nest" the player saw is the NPCs' animations at the nest; a real herd evolution
(generation +0xF0) was not reproduced.

## Fix
`npc.anims` (npc.cpp, game.cpp): the host reports its NPCs' CreaturePlayAnim / PlayAnimation / PlayAnimationTo plays
outside the idle refresh and sends them once a frame to the guests that mirror the NPC (MSG_NPC_ANIM 59, kind 0); once
a second what each plays now (its last BT animation, or a body's) as a catch-up (kind 1). The guest plays them on
the proxy. `coop.fix npc.anims off`: nothing sent or played. Cost: ~11 starts/s for 21 NPCs (12 bytes each) plus
~20 snapshot entries/s (6 bytes each): ~0.3 KB/s per guest; one hook call per animation start on each game.

## Verification
coop_npc_anims.py (suite `npc_anims`, also an NPC attacking the guest's puppet): FAIL with `SPORE_FIX_OFF=npc.anims`
out/lab/20261103-165501 (22 %, fight 0 %), also out/lab/20261103-164130 (15 %, before the fight phase); PASS
out/lab/20261103-165317 (81 %, fight 83 %).

## History
- 2026-11-02: opened.
- 2026-11-03: analysed on fix/w40-creature-acts, not fixed (findings in the docs).
- 2026-11-03: fixed on fix/w40-npc-anims (npc.anims).
- 2026-11-03: released in 0.7.4.
