---
id: BUG-0522
title: Space objects the mod creates on a guest (UFO puppets of the partners) go into the guest's saves
status: open
kind: bug
stage: Space
reported: 2026-11-03
source: split from BUG-0495 (fix/w41-tribe-world)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0495, BUG-0470, BUG-0020
---

## Report
Split from BUG-0495 (its Tribe half fixed on fix/w41-tribe-world, `tribe.save_strip`). Still open: the Space
objects the mod creates on a guest: the partners' UFO puppets (`space_spawn` in modes.cpp: SpawnUFO, a Defender
of the player's empire, `g_ufo_ident`) and the fallback puppet in the avatar's herd (modes.cpp creature_spawn when
the partner's species is unknown: savestrip's owner rule keeps a puppet in the avatar's herd). Not checked whether a
Space save writes the system's UFOs at all, nor which lists hold a UFO (the null rule of BUG-0470: a stripped
reference read back as null where the game reads a pointer). Needs a Space save + reload test as
tribe_save_strip / civ_save_strip.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
