---
id: BUG-0464
title: NPC species evolving in their nest on the host stand still on the guest
status: open
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-npc-anims
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

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-02: opened.
- 2026-11-03: analysed on fix/w40-creature-acts, not fixed (findings in the docs).
