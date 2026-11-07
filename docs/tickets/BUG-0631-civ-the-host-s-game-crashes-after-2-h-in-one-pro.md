---
id: BUG-0631
title: Civ: the host's game crashes after ~2 h in one process (Cell to Civ) when its 2 GB address space is full: a model's vertex blend writes through a null buffer (SporeApp.exe+0x3995F0)
status: fixed
kind: crash
stage: Civ
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/Sterben-11
role: host
versions: 0.7.8
fixed_in: unreleased
branch: fix/w46-addr-space
commits: c7092dea, a39b4b15
tests: mem_reserve, mem_census, mem_census_tour_guest, mem_census_tour_guest_places
signature: dc04aee5:SporeApp.exe+0x3995F0
docs: docs/re/graphics.md#graphics-address-space, docs/06-testing.md#testing-mem-census
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

Measured (lab, lanes 4 / 5, 2026-11-06, fix/w46-addr-space; the census `coop.memcensus` with its allocation tracker,
docs/06-testing.md#testing-mem-census; details docs/re/graphics.md#graphics-address-space):
* Observed: the growth is the game's own heap (EA GeneralAllocator, 16 MB segments that are never given back:
  `priv:by:sporeapp.exe`), not the mod's: no allocation of coop.dll / sporekit.dll grows; DXVK took one 16 MB block.
  The biggest single items are creature skin textures of 5.3 MB each (Skinner code), then models, materials,
  resources and effects.
* Observed: it is a cache of what the game has loaded, not a leak: the guest cycling over the same 12 places of the
  planet for 24 min (6 rounds) cost the host +28 MB in the first round and +3 MB in the 5 after (guest +33, then 0)
  (lab 20261106-210804); 60 puppet removals / re-creations cost 0 on the host (20261106-211958); 20 min of the
  play_creature bots: host -16 MB, guest -7 (20261106-200638, about the same as with the guest's session left).
* Observed: in co-op the host also fills it with what lives around its partner: the guest at a new place every 20 s
  (61 places) grew the host's heap +62 MB, the guest's +28 (20261106-202803); with `npc.view_centres` off (the host
  keeps nothing alive around the guest, BUG-0280) +3 / +4 against +28 / +51 with it on (20261106-204954 /
  20261106-204949, 10 min each). The host keeps the herds, nests and spawns around each partner alive (they are the
  guest's world), with their models and skins; the guest loads the species of the host's NPCs it shows.
* Observed (recorded lab logs of all branches): per-minute loss in a stage stay of over 4 min, median host / guest:
  Creature 4.5 / 3.3 MB/min, Tribe 4.8 / 3.8, Civ 5.4 / 4.7; the host loses more in every planet stage, as in the
  report (host 740 MB in 50 min of Creature, the guest 384).
* Inferred: the report's host filled its 2 GB with this cache over 2 h (two players exploring a planet of many
  species, then Tribe and Civ adding their models), and the space was so fragmented at the end (133 MB free, no block
  over 12 MB) that a 16 MB segment of the allocator or a vertex buffer could not be had: the game did not check the
  null and crashed. The lab cannot run out (Wine gives the lanes 4 GB with the flag off), so the crash itself was not
  reproduced; a full, fragmented space was planted instead (`coop.diag fill`).
* Not a mod bug that can be removed: the cache is the game's (a single player who explores as much fills it too; the
  host fills it about twice as fast because it is two players' world). What the mod can do: keep a contiguous block
  for the end and tell the player in time (the fix), and the large-address flag (below; not shipped).

### The large-address flag (evidence only, not shipped; the owner decides)
* Observed (PE header, 2026-11-06): GOG 2024 `SporeApp.exe` (24 895 536 bytes, dc04aee5) Characteristics 0x0102: the
  flag (IMAGE_FILE_LARGE_ADDRESS_AWARE 0x20) is OFF. The old GOG 2016 exe (922af7c7) 0x0122: ON. The report's log says
  "large-address flag off" (sporekit/diag.cpp reads the same bit).
* Steam (October 2024, 25 066 744 bytes): the Spore ModAPI Launcher Kit (github.com/Spore-Community/ModAPI-Launcher-Kit,
  ModAPI.Common/GameVersion.cs `IsLAACompatible`) says its exe carries SteamStub DRM that "prevents modification of the
  exe, thus preventing the game from running if LAA is set in PE header"; for GOG October 2024 "no known issues". The
  Launcher Kit only checks the flag (LAAUtils.IsLAA) and warns on Steam; it does not set it. Its warning text: "The
  LAA/4GB patch is not required to use mods, but may help improve game stability."
* The community 4GB patch (NTCore, ntcore.com/4gb-patch) sets that one bit in the PE header (newer versions also fix
  the header checksum) and keeps a .bak of the exe. On 64-bit Windows a flagged 32-bit process gets 4 GB instead of 2.
* Safe for the code? The GOG 2016 build shipped with the flag on, and every lab game of the GOG 2024 code runs in a
  4 GB space (Wine gives the lanes 4095 MB with the flag off: the perf lines say "of 4095 MB"), with allocations above
  2 GB, in all stages, without a crash class of its own. Not checked: Windows itself with the flag (no Windows lane).
* A mod cannot set it at runtime: the loader reads the flag when it creates the process (the user address space limit
  is fixed then); our steam_api.dll proxy and SporeKit load inside the running process, too late. Ways: a patched exe
  (GOG only: one bit; a game update or "verify files" puts the original back) or a launcher that starts a patched
  copy. Steam: not possible without breaking SteamStub. Wine / Proton players already get 4 GB (Proton's
  PROTON_FORCE_LARGE_ADDRESS_AWARE / Wine's WINE_LARGE_ADDRESS_AWARE; the lab's Bottles runner does it).

## Fix
`core.mem_reserve` (mods/coop/diag_stats.cpp, BUG-0631): at the start the game keeps 128 MB of address space in one
block (MEM_RESERVE | MEM_TOP_DOWN, nothing committed: no RAM is used). When the address space runs low (the check of
the minute's perf line: under 300 MB free or no free block of 48 MB) the block is given back to the game, once, and
the player's screen shows "Your game is running out of memory: save and restart it soon (Spore is a 32-bit game and
keeps what it loads until it is closed)" (again every 10 minutes while it stays low). The game gets one last big
contiguous block for its 16 MB segments and buffers instead of crashing on the first one that does not fit, and the
player gets minutes to save (the report's host had 10 minutes between the first low warning and the crash).
`coop.fix core.mem_reserve off`: no block, the low state only in the log (0.7.9). It does not make the cache smaller.
Test tools: `coop.memcensus` (the census and its tracker), `coop.diag fill <MB> [piece]` (a full fragmented space),
`coop.diag reserve` (the block's state).

## Verification
`mem_reserve` (tools/tests/coop_mem_reserve.py): the host's address space filled with 16 MB blocks to ~260 MB free,
no free block over 16 MB (`coop.diag fill 260 16`); within 2.5 min the game's own space (a census of its process) has
a free block of 100 MB or more and the host's screen shows the notice; 3 min of the play_creature bots after, no fault.
* FAIL: 20261106-220852 (`SPORE_FIX_OFF=core.mem_reserve`: the largest block stayed 16 MB, no notice).
* PASS: 20261106-220854 (a 127 MB block back after 80 s, the notice at 72 s; after 3 min of play 418 MB free; the
  lab's mem-low gate failed it on the planted low state, allowed since), 20261106-221554 (in the regression run, same
  gate), 20261106-225320 (OK with the allow).
* Regression (SPORE_DETECTOR=20, lanes 4 / 5): 20261106-221554 smoke + play_creature + play_tribe + mem_census_short:
  all OK but cell_coop (BUG-0395's known flake, OK on rerun 20261106-225320), frames_loop (OK on rerun
  20261106-225320) and unit (3 free-lane lock self-tests, the same 3 also fail on fix/w45-remote-bodies-2 and
  fix/w45-last: the lab environment, not this branch).
* Measurement runs (the cause, above): 20261106-200638, 20261106-202803, 20261106-204949, 20261106-204954,
  20261106-210804, 20261106-211958, 20261106-213314, 20261106-213316, 20261106-214428, 20261106-214428-2.
* Not verified: Windows with the 2 GB space (no Windows lane); the crash itself (the lab has 4 GB).

## History
- 2026-11-06: opened. Triage from the crash file, the dump's stack scan and the host's log (fix/w45-intake).
- 2026-11-06 (fix/w46-addr-space): measured with a census of the address space per stage (new `coop.memcensus`): the
  game's own cache, filled faster on the host by its partner's surroundings, no leak of the mod; fixed the crash side
  with core.mem_reserve (a kept block given back with a notice); the large-address flag checked (off in GOG 2024,
  impossible on Steam's SteamStub exe, not settable at runtime). Status fixed.
