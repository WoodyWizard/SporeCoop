---
id: BUG-0733
title: Creature: the partner's creature keeps the size it had at spawn on the other screen (brain upgrades not shown)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-08
source: BUG-0418 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2b-content-b
commits: 8911e865
tests: creature_grow, creature_grow_slow (coop_creature_grow.py); make test (net_tests: PlayerState body)
signature:
docs: docs/re/creature.md#creature-player-species-callers
related: BUG-0418, BUG-0732, BUG-0461
---

## Report
K2b audit of the class BUG-0418: the partner's puppet got its size once at its spawn (modes.cpp creature_spawn: its own game's pick x 1.15) and kept it; PlayerState had no creature size: neither player saw the other grow with the brain upgrades.

## Root cause
Observed (lab 20261108-040640, `SPORE_FIX_OFF=creature.puppet_scale`): after brain level 1 both creatures were 1.15 in their own games, both puppets stayed 1.15 (1.0 x 1.15 of the spawn) where 1.3225 was the partner's size x 1.15.

## Fix
`creature.puppet_scale`: PlayerState carries the sender's own creature's size (`body`, cSpatialObject GetScale in the Creature stage; appended after agl, an older reader stops before it, an older sender reads as 0 = not sent); the puppet is set to body x 1.15 (the spawn's factor since 0.6, kept) when it differs by more than 1 %. `coop.fix creature.puppet_scale off`: the spawn's size (0.7.10).

## Verification
FAIL: out/lab/20261108-040640 (`SPORE_FIX_OFF=creature.puppet_scale`: puppets 1.15 / 1.15, the partners 1.15 x 1.15). PASS: out/lab/20261108-034334 (creature_grow: 1.32 / 1.316 for 1.3225), out/lab/20261108-041412 (the _slow twin at 20 fps over the player network).
Re-run on the merged branch fix/w46-k2b-content (the run dirs above were in the sub-worktrees fix/w46-k2b-content-a / -b, removed 2026-11-08; their ids stay as the record): FAIL out/lab/20261108-065929, PASS out/lab/20261108-054842.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): fixed, `creature.puppet_scale`; FAIL and PASS runs above.
- 2026-11-08: FAIL / PASS runs repeated on the merged K2b branch.
