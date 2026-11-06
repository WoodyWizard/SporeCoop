---
id: BUG-0631
title: Civ: the host's game crashes after ~2 h in one process (Cell to Civ) when its 2 GB address space is full: a model's vertex blend writes through a null buffer (SporeApp.exe+0x3995F0)
status: open
kind: crash
stage: Civ
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-11
role: host
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature: dc04aee5:SporeApp.exe+0x3995F0
docs:
related: BUG-0621, BUG-0454, BUG-0127
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/Sterben-11: the host's game (GOG 2024 dc04aee5, large-address
flag off) crashed in Civ at frame 407827, about 2 h after the game started (one process, Cell > Creature > Tribe >
Civ). Crash file crash-20261106-192552.txt and its .dmp were written (BUG-0127's path worked).

## Root cause
Observed (crash file): 0xC0000005 write to 0x0000000C at SporeApp.exe+0x3995F0 (exe 0x7995F0); stack +0x399F99
+0x3A177D +0x3A19B0 +0x3A1FEE +0x3A3017; address space 1918 of 2047 MB used. Further down the stack scan:
MallocInternal+0x30, MallocAlignedInternal+0x749, cModelWorld::FinishBackgroundLoad+0x1060, cAppSystem::PostUpdate
(the main thread finishing a model's background load).

Observed (objdump, GOG 2024, not named in the .sym files):
* 0x799590 (fault at 0x7995F0 `movss [eax],xmm0`, eax = out = 0xC): cdecl(int kind 1..5, float t, a*, b*, out*), a
  blend of two vectors by kind (kind 1: out = a*(1-t) + b*t, two floats).
* 0x799F71 (return 0x799F99): a wrapper choosing the kind from the table 0x153B208.
* 0x7A177D: a loop over vertex streams, out = stream base + idx * stride: 0xC = a null stream base at element 1 with
  a 12-byte (vec3) stride.
Inferred: the destination vertex buffer was not allocated (null, unchecked by the game) because the address space was
full: the largest free block was 12 MB.

Observed (the host's sporekit.log, perf lines, free / largest block MB): 17:25 start 1935; 17:44 Cell 1127 / 563;
18:32 end of Creature 389 / 112; 19:06 end of Tribe 361 / 112; 19:12 Civ 254 / 80; 19:22 Civ 128 / 12; the mod's
"mem: the game's address space runs low" at 19:15, 19:20, 19:25:13; the crash at 19:25:52. BUG-0454's "texture without
a raster" guard fired 4 times in the host's editor at 18:09 and once at 18:40 (the same null-allocation pattern
earlier). The host lost ~740 MB in 50 min of Creature, the guest ~384 MB in the same time (1060 -> 676); stage changes
give almost nothing back. The same host drain in Creature on 2026-11-04 with 0.7.5 (1038 -> 517).
Not found in the log: what grew it (6 world-share saves, effects ~20-40 creates a minute, 113 creations lines).
Inferred, not proven: the host simulates the NPCs for the guest in Creature ("npcs simulated"), which may be why the
host drains twice as fast.

Same class as BUG-0621 (the free address space drained; that one a 4-player Cell guest at 1-3 fps with the flag on),
another scenario: a host on the 2 GB space draining slowly in normal play and fragmenting, mostly in Creature.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: one host process Cell -> Creature (30-50 min with a guest, play_creature / the long-session bot) -> Tribe
     -> Civ with the large-address flag off, `perf` free / largest block every minute on both games; the same route
     solo without the mod for the game's own drain. A VirtualQuery snapshot (region sizes by type) before and after
     Creature on the host shows what grows. -->

## History
- 2026-11-06: opened. Triage from the crash file, the dump's stack scan and the host's log (fix/w45-intake).
