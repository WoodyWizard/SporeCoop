---
id: BUG-0479
title: Space: the partner's ship and NPC ships are drawn at another scale on the other game (4.82 vs 0.84)
status: fixed
kind: desync
stage: Space
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
role: 
versions: 
fixed_in: 0.7.4
branch: fix/w40-scale
commits: 59c64045 3fa7d38c d4f3ff94 cfa77d8b f07fcbb0
tests: mirror_scale_space (tools/tests/coop_mirror_scale.py --kind ufo)
signature:
docs: docs/re/space.md#space-ufo-scale
related: BUG-0419
switch: space.puppet_scale
---

## Report
Detector: `Space:scale:ufo` in 97% of 126 Space comparisons (e.g. 4.82 host, 0.84 guest; 7.23 / 1.26 earlier); `Creature:scale:ufo` once. A mirror / puppet ship gets another scale than the original (the game scales UFOs with the camera distance or the planet / galaxy view?). To check: what the player sees (screenshot pair), then which scale is right. Also `Space:noun_only_host:planetary_artifact` (9 artifacts in the host's game only) in 4%.
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: the game sizes every UFO each frame (SporeCoop::UFO_UpdateScale 0xC3B550 -> loco vt 0x40 cModelObject_SetScale 0xC88630, factor UFO_ViewScale 0xC3AFD0 by view, camera distance and type: the player's own ship type 0 has its own tuning entry). A partner's ship is a Defender NPC UFO here (modes.cpp space_spawn): in the space fixture's system the own ship is 4.946 on both games, each partner's puppet 3.297. So each player saw the other's ship at an NPC ship's size (two thirds); the detector's pairs are the host's own ship vs the guest's puppet of it. The right size is the player ship's (what the partner sees of its own ship). A size set from outside lasts one frame (4.946 set, 3.297 again 2 s later). Screenshots: out/shots/w40s_space_host.png / _guest.png in the worktree (ships too small at the default zoom to judge by eye). Inferred: the night's 4.82 / 0.84 is the same on a planet.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
Not a field (the game recomputes it every frame): squadron.cpp hooks cModelObject_SetScale; when the game sizes a partner's puppet (the UFO loco vtable, puppet_owner >= 0) the puppet gets the own ship's current size. `coop.status` line `space puppet scale:`. Switch `coop.fix space.puppet_scale off`.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
FAIL with SPORE_FIX_OFF=space.puppet_scale: out/lab/20261103-124246 (8 of 8 pairs, 4.946 vs 3.297, detector Space:scale:ufo 4 of 4). PASS: out/lab/20261103-125153 and out/lab/20261103-123348 (0 of 8, detector 0 of 4). NPC ship mirrors: none in the fixture's area, not measured.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: fixed on fix/w40-scale (replicated fields, docs/sync-architecture-plan.md#sync-fields).
- 2026-11-03: released in 0.7.4.
