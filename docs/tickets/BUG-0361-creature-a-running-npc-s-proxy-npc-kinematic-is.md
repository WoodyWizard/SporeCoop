---
id: BUG-0361
title: Creature: a running NPC's proxy (npc.kinematic) is teleported every frame and shows no walk gait (slides)
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: found on fix/w29-puppet-move (BUG-0360 measurement)
role: guest
versions: 0.7.3.2
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-puppet-walk
related: BUG-0360, BUG-0281
---

## Report
Found while measuring BUG-0360: a creature teleported every frame (creature spatial vt 0x44,
`game::teleport`) keeps the animation's walk gait state (`+0xB54 -> +0x17C -> +0x15B8`) at 0 in
97-100 % of its moving frames and its main animation stays the plain idle 0x2481DE5: it slides
over the ground without walking. `npc.kinematic` (BUG-0281) places the proxy of a running host NPC
on a guest exactly like that every frame, so running NPCs probably slide on guests (inferred, not
measured on proxies; BUG-0281's note "stills show running poses" is from screenshots).

How to check: extend `coop.track` (coop.cpp) to NPC proxies (or sample one with `coop.npc`), run
`creature_npc_dash` and read the gait state of the running proxies.

## Root cause
Teleport (0xC0C040) sets creature +0xB5D = 1, which restarts the animation's gait (verified for the
partner's puppet). SetPosition (0xC0DEA0) does not. The fix used for the partner's puppet
(`creature.puppet_walk`: the game walks it with a MoveTo ahead and the speed, SetPosition puts it on
the path every frame) would likely work for proxies too.

## Fix
Not fixed (found on fix/w29-puppet-move, outside its task).

## Verification
Open.

## History
- 2026-11-01: opened.
