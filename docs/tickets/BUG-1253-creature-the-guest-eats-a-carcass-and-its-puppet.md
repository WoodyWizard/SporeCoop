---
id: BUG-1253
title: Creature: the guest eats a carcass and its puppet in the host's game stands idle
status: open
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video, second analysis
role: host
versions: <0.7.15
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
A players' video: the guest eats a carcass; in the host's game its puppet stands next to it idle. Suggested cause: the eat behaviour node plays its animation through CreaturePlayAnim (0xC122F0), not PlayAnimation (game.cpp); set_behaviour_anim_listener is wired only to NPCs (npc.cpp), and the players' avatars (puppets.cpp) relay only PlayAnimation.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
