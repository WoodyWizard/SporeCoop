---
id: BUG-0507
title: guest crash in Creature: drive_puppet calls set_speed on a freed puppet after a 4 s hitch (privileged instruction 0xC0000096, 3010a413:windows.storage.dll+0x37626D, coop.dll set_speed+walk_place)
status: fixed
kind: crash
stage: Creature
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: guest
versions: 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-guest-combat
commits: d581fbba, 65730701
tests: coop_puppet_freed.py
signature: 3010a413:windows.storage.dll+0x37626D 0xC0000096 (EIP = string bytes 'mbIs' read from past the end of a vtable), coop.dll 0.7.3.6 +0xCA0AC game::set_speed <- +0xD464E drive::walk_place <- +0xD2324 modes::creature_drive <- +0x53890 drive_puppet
docs: docs/re/creature.md#creature-puppet-alive
related: BUG-0091, BUG-0426, BUG-0347
---

## Report
Guest on 0.7.3.6 (Steam build), Creature stage, about an hour into a session over the internet (rtt ~150 ms), report 20261104-014642 (crash-20261104-014642.txt + .dmp). Right before: `perf: hitch 4106 ms`, `device Reset (1920x1080)`, two 200 ms hitches, then the crash 0.4 s later in the game's main thread. The same game had several 17-38 s freezes with a device Reset after each earlier in the session (the window was minimized or alt-tabbed, see BUG-0513). The player's text: 'CRASH (unhandled exception)'.
Target: 0.7.4 (a crash, Creature).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (crash .txt, its minidump, coop.dll built from tag v0.7.3.6 and symbolized): the stack holds coop.dll+0xCA0AC (game::set_speed, game.cpp:857: vcall slot 0xC4 on spatial(c) = c+0xC0) <- +0xD464E drive::walk_place <- +0xD2324 modes::creature_drive <- +0x53890 (anonymous)::drive_puppet (coop.cpp:696). In the dump ebp = c = 0x303EE928, whose first dword (its vtable) is 0x3AF0D7D0, a heap address, not a vtable of the exe; ecx = spatial = c+0xC0 holds 0x01F536D8 = 0x14736D8 in the exe: a short cModelObject-family vtable (slot 0x40 = cModelObject_SetScale 0xC88630) that ends before 0xC4; its 'slot 0xC4' is the string bytes 'mbIs' (0x7349626D) of the following rdata, and the CPU jumped there: a privileged instruction inside windows.storage.dll.
So the pointer the puppet driver used was not a creature any more: its memory was freed and reused by another object (a use after free of the partner's puppet), right after a long frame gap. Inferred: the puppet was destroyed or replaced (the partner 'hidden' / 'appeared in the world' path, a stage-time cleanup, or the game freeing it) while Remote kept the raw pointer; nothing checks it is still alive before driving it. To check: what destroys the puppet during or after a frame gap of seconds (state older than 5 s -> hidden), and make drive_puppet check liveness (the object list / pool generation) as CLAUDE.md 2 asks.
Observed in the code (rel/0.7.4): the Creature and Adventure adapters had no `alive` callback, so neither
`puppet_now()` nor `drive_puppet()` ever checked the raw `Remote::puppet` pointer; a puppet the game destroyed
was driven every frame for as long as the partner was shown. Observed in the lab: the game's own destroy
(DestroyInstance) on the puppet is not noticed with the check off (the partner stays missing, the dead pointer is
driven); two device Resets do not free the puppet. Not known: what freed it on the player's PC (no hide or
despawn line of ours before the crash; a 4.1 s frame and a device Reset right before).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
`creature.puppet_alive` (modes.cpp `creature_alive`, the adapters' `alive`): a Creature puppet is alive only
while the game's object list holds it as a live creature with the cGameData serial it had at its spawn (once per
frame and puppet). A dead one leaves the puppet list and creature_hp, drive_puppet shows the partner again with
a new puppet; every `puppet_now()` caller gets null for it. Off: no check (0.7.3.6).
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
coop_puppet_freed.py (suite puppet_freed).
* FAIL with `SPORE_FIX_OFF=creature.puppet_alive`: out/lab/20261103-171411 (3 rounds: the dead puppet kept, listed=0).
* PASS: out/lab/20261103-171523 (2 device Resets, 3 game destroys, a new live puppet at the host each time).
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened from report 20261104-014642 (guest, 0.7.3.6); symbolized with a v0.7.3.6 build (out of tree).
- 2026-11-03: fixed on fix/w40-guest-combat (creature.puppet_alive), reproduced with the game's own destroy on lane 2.
- 2026-11-03 (fix/w40-gh-issues): GitHub issue #194 (host, 0.7.3.6, GOG 2024): the same crash (EIP 0x7349626D, same coop.dll frames and registers), every time a UFO of the game's abduction event took a creature near the players (the player's text); also crash-20261102-163856 (0.7.3.3) of the same PC. So the host gets it too, and the inferred trigger there is the UFO event freeing the partner's puppet. Ticket BUG-0520 (duplicate of this one).
- 2026-11-03: released in 0.7.4.
