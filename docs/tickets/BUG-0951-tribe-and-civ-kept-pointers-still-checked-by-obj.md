---
id: BUG-0951
title: Tribe and Civ: kept pointers still checked by object_alive on their own memory (chiefs ring, epic, tools, vehicle and city tables): move them to game::LiveRef
status: open
kind: crash
stage: Tribe
reported: 2026-11-12
source: fix/w48-freed (0.7.13 L1), code read
role: both
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0426, BUG-0844, BUG-0842
---

## Report
Found while fixing package L1 of 0.7.13 (fix/w48-freed); no player report. The L1 fixes moved the NPC proxies, the
nest herds, the Tribe mirrors, the deferred destroys, the effect and animation handlers to `game::LiveRef`
(the object list of the frame + the serial, docs/re/creature.md#creature-freed-block-reuse). About 60 other
`object_alive(<kept pointer>)` checks in tribe.cpp and civ.cpp (the ones BUG-0426 lists: tribe.cpp chiefs, the target
ring, the epic, held tools; civ.cpp vehicle and city tables) still read the kept object's own memory, which cannot
tell a freed block or a new object of the same type at that address (a creature's block comes back within 0.3-1.3 s,
a citizen's too).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Inferred from the code (fix/w48-freed): the same pattern as BUG-0842 / BUG-0844; not reproduced per site.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix idea
Store a `game::LiveRef` (pointer, serial, noun) where a pointer is kept across frames and ask `game::alive` before any
call; a lint in make test against `object_alive` on a stored pointer (BUG-0426's "General fix"). One test per family
with `coop.npc reusehunt` / `coop.tribe reusehunt` (docs/06-testing.md#testing-freed-block-reuse).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
