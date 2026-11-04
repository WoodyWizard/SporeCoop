---
id: BUG-0454
title: host crash in the creature editor at RenderWare::Raster::Fill+0x7 again: twice in 15 minutes on 0.7.3.4 (BUG-0371 said out of memory after 12 h)
status: fixed
kind: crash
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 0.7.3.3, 0.7.3.4
fixed_in: 0.7.4
branch: fix/w40-crashes
commits: 0074b298
tests: editor_desync
signature: dc04aee5:SporeApp.exe RenderWare::Raster::Fill+0x7
docs: docs/re/graphics.md#graphics-null-raster, docs/known-issues.md#issue-editor-skin-painter-oom
related: BUG-0371, BUG-0442
---

## Report
Host crashes 2026-11-02 18:21:28 and 18:34:02 (dumps crash-20261102-182128.dmp / -183402.dmp, both SporeApp.exe+0xDF02C7 = dc04aee5 0x11F02C7 RenderWare::Raster::Fill+0x7, read 0x4), host in its editor, same as the 2026-11-01 crash judged a game out-of-memory (BUG-0371, game-bug). Two in 15 minutes do not fit an address space exhausted after 12 h. 0.7.3.4 added report screenshots (BUG-0442: StretchRect / GetRenderTargetData in Present when a mark is made; every player shoots on a partner's mark): the guest marked at 18:21:20 and 18:32:57 (guest clock), close to both crashes. Check: address space in the dumps / logs, whether a report shot ran in the editor just before, the skin painter's raster allocation vs our render target. The .txt of these two crashes were only posted as Discord previews (not downloaded).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (dumps crash-20261102-182128.dmp / -183402.dmp): ecx = esi = ebp = 0 at Fill; the caller is `SporeCoop::Texture_BuildMips` 0x7AFFC0 (from the skin painter, ExecutePipeline 0x51CCE2): the texture manager's vt 0x3C returned a texture object whose raster (+0) is null (flags +4 bit 0 set), and the level loop called Raster::Fill on it. No coop.dll / sporekit.dll frame.
Not our screenshot (BUG-0442): the same crash on 0.7.3.3 (2026-11-01 13:50, before report screenshots existed); on 0.7.3.4 the guest's marks were 8 s and 65 s before the crashes, and the shot releases every surface in the same Present. Address space was not short (0.7.3.3: 868 MB free, largest block 346 MB): BUG-0371's out-of-memory verdict is superseded.
Inferred: the graphics driver did not make that texture (only this host: AMD Radeon RX 9060 XT, amdxn32 32.0.32015.7029); why is not known.

## Fix
mods/coop/gamefixes.cpp: a hook on RenderWare::Raster::Fill: a null raster takes Fill's own failure return (0, as when Raster_Lock fails) instead of the fault. Switch `coop.fix game.null_raster_fill off`. The editor then has that skin texture without paint for the bake (not seen in the game).

## Verification
no-fail-run: the driver's failure to make a texture does not happen on the lab's GPUs (Wine, Mesa), so the null raster cannot be made the way the host got it; the guard only adds the null check Fill's own failure path already has (Fill returns 0 when the raster cannot be locked, and its callers handle 0: 0x7AFFC0 ignores the result). Regression: the editors in editor_desync / play_creature.

## History
- 2026-11-02: opened.
- 2026-11-03: analysed (not the report screenshot; a texture without a raster), guarded on fix/w40-crashes (game.null_raster_fill).
- 2026-11-03 (fix/w40-gh-issues): GitHub issue #191 (host, 0.7.3.4 GOG 2024, NVIDIA RTX 5060 Ti, 3 players, Creature editor): "the game crashes right after clicking Save". Observed in its report zip (crash-20261103-004744): the same fault, 0xC0000005 at RenderWare::Raster::Fill+0x7 data 0x4, ecx = esi = ebp = 0, calls 0x7B0042 / 0x7B009F (Texture_BuildMips) <- 0x51CCE2 (cSkinPainterPipeline::ExecutePipeline): the skin bake the editor runs for the save. So not only the AMD host of the first reports (the inference "the AMD driver" above is too narrow). Seen before it in the same editor visit: a 32 s frame with a device Reset (window away) 57 s before the crash, and address space 879 MB free but the largest free block only 256 MB (large-address flag off): inferred, a texture the game could not allocate in the fragmented address space. The same report's other dump (crash-20261102-232741, Creature) is BUG-0453. Issue #192 (a guest of the same kind of session, 0.7.3.4, "crashes after saving the creature", no files): inferred the same crash, not verified. Covered by game.null_raster_fill (0.7.4); the save then has that skin texture without paint at worst.
- 2026-11-03: released in 0.7.4.
