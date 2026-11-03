---
id: BUG-0371
title: Host crash in the creature editor: RenderWare::Raster::Fill+0x7 on a null raster from the skin painter (dc04aee5:SporeApp.exe RenderWare::Raster::Fill+0x7)
status: game-bug
kind: crash
stage: Editor
reported: 2026-11-01
source: player reports 2026-11-01 (host GOG 2024 + Steam guest, 0.7.3.3)
role: host
versions: 0.7.3.3
fixed_in:
branch:
commits:
tests:
signature: dc04aee5:SporeApp.exe RenderWare::Raster::Fill+0x7
docs: docs/known-issues.md#issue-editor-skin-painter-oom
related: BUG-0127
---

## Report
Host crash in its creature editor 55 min into a Cell > Creature session (GOG 2024 relocated to
0x150000, Windows, coop 0.7.3.3): 0xC0000005 at RenderWare::Raster::Fill+0x7 (0x11F02C7 numbered
from 0x400000), read of 0x4, ecx = esi = 0 (a null raster), stack scan through
Skinner::cSkinPainterPipeline::ExecutePipeline+0x1D02 (0x51CCE2) and 0x750042 (FUN_0074FF10, a
job of the texture manager; the crash file names it by the nearest symbol cTextureManager__ctor).
Game address space 1554 of 2047 MB used, large-address flag off. The "up 43290 s" is the PC's uptime.

## Root cause
Inferred: the game's own out-of-memory in the skin painter (a raster it did not get, used unchecked).
Observed: no coop.dll frame; the mod hooks nothing in the skin painter / texture path; the host's
address space over the session (perf lines): Cell ~1150 MB free, Creature 1000 -> 880, each editor
takes more (13:31 939 -> 13:33 763 MB free, largest block 413 -> 181 MB); 12 s before the crash 868
MB free (largest 346 MB), at the crash 493 MB free. The minidump has no memory list (not checked).

## Fix
None in the mod. Players with the large-address flag off: the 4GB patch.

## Verification
no-fail-run: a game crash, not reproduced; triage only.

## History
- 2026-11-01: opened from the host's crash in the 2026-11-01 reports; game-bug (out of memory).
- 2026-11-02: two more host crashes at the same address within 15 minutes on 0.7.3.4 (not 12 h of play): reopened as BUG-0454 for analysis; the out-of-memory verdict is in doubt.
