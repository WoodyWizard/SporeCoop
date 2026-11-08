---
id: BUG-0669
title: Tribe: the guest's mirrors of walking members make almost no footstep sounds
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-07
source: found in fix/w46-tribe-civ (BUG-0646 analysis)
role: guest
versions: 0.7.9
fixed_in: 0.7.10
branch: fix/w46-misc
commits: 05f4de4a
tests: tribe_footsteps
signature:
docs: docs/re/tribe.md#tribe-epic-steps
related: BUG-0646, BUG-0600
---

## Report
Found while recording both games' audio for BUG-0646 (tribe fixture, lane 4, 2026-11-07, `coop.audio mark` / `list`, day length 30 s, the host's 9 members going about their village work for 3 min): sound 0x4A776923 (type 3, played from 0x9A50A7 <- 0x9A6BAE <- 0x9A6CBC <- 0xA01282) 436 times on the host, 36 times on the guest; the guest instead played 0x76BDAADD (same path) 565 times against the host's 84.

## Root cause
Observed (Ghidra + lab lane 9, docs/re/tribe.md#tribe-epic-steps): the "sound" 0x4A776923 of the report is the foot's
sound type parameter 0x39FA0AB of the "foot_step" play (sound id 0), not a sound id. FUN_009A6BD0 is called by the
animation update 0xA01000 only while a gait blend runs or the animation's epic flag (AnimatedCreature +0x17C -> +0x2D5,
parameter 0x48DEF6D) is set; the creature's model setup 0xC21D50 sets it from the tuning's isEpic (+0x388). Every
footstep the host's game played (35-52 a minute, flag 1) was the fixture's epic (species 0654E5CB); the tribe members
(no tuning) make footsteps only during gait blends, on both games alike. The guest's proxy of the epic got the host's
tuning after its model setup (BUG-0462 SetTuning), so its flag stayed 0 and it walked silently (4 of 5 runs; in one
run the proxy was made with its tuning and had the flag). The velocity +0x1660 was not the cause: the mirrors' and
proxies' animation velocity moves like the host's.

## Fix
npc.cpp proxy_epic_steps: every frame a proxy's animation epic flag follows its tuning's isEpic (0xA04C00, the game's
own setter) while the host's NPC walks (reported speed > 0.3 m/s): a standing proxy is turned a little each frame by
the mod's facing and would step on the spot (256 footsteps against the host's 38 without that rule). `coop.npc untune`
(tests) clears the flag too. `coop.audio` records float parameters (AudioSystem vt 0x3C hook). Switch:
`coop.fix npc.epic_steps off`.

## Verification
tribe_footsteps (the guest's proxy of the epic put into the made-in-a-herd-of-ours state with `coop.npc untune`;
the epic-flag footsteps of both games over 60 s, the guest's within half to twice the host's):
FAIL with SPORE_FIX_OFF=npc.epic_steps: out/lab/20261107-120238 (0 vs 40);
PASS: out/lab/20261107-120034 (33 vs 36).

## History
- 2026-11-07: opened.
- 2026-11-07: fixed in fix/w46-misc (npc.epic_steps): the footsteps were the epic's; the guest's proxy lacked the animation epic flag.
- 2026-11-07: released in 0.7.10.
