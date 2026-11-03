---
id: BUG-0507
title: guest crash in Creature: drive_puppet calls set_speed on a freed puppet after a 4 s hitch (privileged instruction 0xC0000096, 3010a413:windows.storage.dll+0x37626D, coop.dll set_speed+walk_place)
status: open
kind: crash
stage: Creature
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: guest
versions: 0.7.3.6
fixed_in:
branch:
commits:
tests: 
signature: 3010a413:windows.storage.dll+0x37626D 0xC0000096 (EIP = string bytes 'mbIs' read from past the end of a vtable), coop.dll 0.7.3.6 +0xCA0AC game::set_speed <- +0xD464E drive::walk_place <- +0xD2324 modes::creature_drive <- +0x53890 drive_puppet
docs:
related: BUG-0091, BUG-0426, BUG-0347
---

## Report
Guest on 0.7.3.6 (Steam build), Creature stage, about an hour into a session over the internet (rtt ~150 ms), report 20261104-014642 (crash-20261104-014642.txt + .dmp). Right before: `perf: hitch 4106 ms`, `device Reset (1920x1080)`, two 200 ms hitches, then the crash 0.4 s later in the game's main thread. The same game had several 17-38 s freezes with a device Reset after each earlier in the session (the window was minimized or alt-tabbed, see BUG-0513). The player's text: 'CRASH (unhandled exception)'.
Target: 0.7.4 (a crash, Creature).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (crash .txt, its minidump, coop.dll built from tag v0.7.3.6 and symbolized): the stack holds coop.dll+0xCA0AC (game::set_speed, game.cpp:857: vcall slot 0xC4 on spatial(c) = c+0xC0) <- +0xD464E drive::walk_place <- +0xD2324 modes::creature_drive <- +0x53890 (anonymous)::drive_puppet (coop.cpp:696). In the dump ebp = c = 0x303EE928, whose first dword (its vtable) is 0x3AF0D7D0, a heap address, not a vtable of the exe; ecx = spatial = c+0xC0 holds 0x01F536D8 = 0x14736D8 in the exe: a short cModelObject-family vtable (slot 0x40 = cModelObject_SetScale 0xC88630) that ends before 0xC4; its 'slot 0xC4' is the string bytes 'mbIs' (0x7349626D) of the following rdata, and the CPU jumped there: a privileged instruction inside windows.storage.dll.
So the pointer the puppet driver used was not a creature any more: its memory was freed and reused by another object (a use after free of the partner's puppet), right after a long frame gap. Inferred: the puppet was destroyed or replaced (the partner 'hidden' / 'appeared in the world' path, a stage-time cleanup, or the game freeing it) while Remote kept the raw pointer; nothing checks it is still alive before driving it. To check: what destroys the puppet during or after a frame gap of seconds (state older than 5 s -> hidden), and make drive_puppet check liveness (the object list / pool generation) as CLAUDE.md 2 asks.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened from report 20261104-014642 (guest, 0.7.3.6); symbolized with a v0.7.3.6 build (out of tree).
