---
id: BUG-0323
title: Space: a guest faults at 0xE98DD0 (effect object null) when the host's ship appears in an NPC system, lane 8 only
status: open
kind: crash
stage: Space
reported: 2026-10-30
source: lab runs 20261030-145308, -150155, -150848 (fix/w13-space) and 20261030-151438 (main 4dbacc5), lane 8
role: guest
versions: main 4dbacc5 and fix/w13-space
fixed_in:
branch:
commits:
tests:
signature: 0xC0000005 at 0x00E98DD0 (labelled cPlanetGfx_Create+0x8F0, really FUN_00e98d90+0x40) data 0x4
docs:
related:
---

## Report
Lab, lane 8 only (GOG 2024), 2026-10-30 from ~15:00: every `coop_space_jump.py` /
`coop_space_seed_profile.py` run on lane 8 (5 of 5, also main 4dbacc5 in lab 20261030-151438 and
the branch with the BUG-0185 fix switched off) had a caught guest fault ~1.4 s after the host's
ship appeared in the NPC system 0046101B the guest had jumped to: `0xC0000005 at 0x00E98DD0 ...
(data 0x00000004)`, eax/esi 0, calls 00E9AE74 00E9290C 00E9294D 00E9B217 00E9950B 00B37D2E
00B3687C 00A44E28 00C37003 00BD790D. The game went on; the lab fails the test on the fault count.
Lane 9 ran the same tests 8 times in the same hour without it; lane 8 ran space_jump 5 times
clean at 13:35-13:50 (lab 20261030-133510) with the same code.

## Root cause
Observed (Ghidra GOG 2024): 0xE98D90 calls a virtual getter (vt+0x2C) on its object and writes
`[result+4] |= 1` (or `&= ~1`) without a null check; the getter returned null. The function is
called from 0xE9A920, 0xE9B180, 0xE9B8C0, 0xE9D2E0 (effects / gfx code; the label
"cPlanetGfx_Create+0x8F0" is only the nearest symbol). Inferred: the guest makes the host's puppet
ship (or its effect) when the host appears; something on lane 8 (a cache or profile file written
by an earlier test between 13:50 and 14:50: the regression run had space_puppet,
campaign_space, space_play, space_captain on lane 8) makes one of its effects missing. Not
analysed further.

Next steps: compare lane 8's and lane 9's guest profile / cache folders (instances 16 and 18);
`spore.sh lane` recreate lane 8 and see whether it goes away; a dump at the fault.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
