---
id: BUG-0454
title: host crash in the creature editor at RenderWare::Raster::Fill+0x7 again: twice in 15 minutes on 0.7.3.4 (BUG-0371 said out of memory after 12 h)
status: open
kind: crash
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
docs:
related:
---

## Report
Host crashes 2026-11-02 18:21:28 and 18:34:02 (dumps crash-20261102-182128.dmp / -183402.dmp, both SporeApp.exe+0xDF02C7 = dc04aee5 0x11F02C7 RenderWare::Raster::Fill+0x7, read 0x4), host in its editor, same as the 2026-11-01 crash judged a game out-of-memory (BUG-0371, game-bug). Two in 15 minutes do not fit an address space exhausted after 12 h. 0.7.3.4 added report screenshots (BUG-0442: StretchRect / GetRenderTargetData in Present when a mark is made; every player shoots on a partner's mark): the guest marked at 18:21:20 and 18:32:57 (guest clock), close to both crashes. Check: address space in the dumps / logs, whether a report shot ran in the editor just before, the skin painter's raster allocation vs our render target. The .txt of these two crashes were only posted as Discord previews (not downloaded).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-02: opened.
