---
id: BUG-1098
title: Space: guests' screens show broken textures, a white wedge, garbage geometry and no HUD after the Civ -> Space entry (campaign_space key frames, 6-lane final check)
status: not-reproduced
kind: test
stage: Space
reported: 2026-11-14
source: lab 20261114-005006 campaign_space (rel/0.7.14 final check)
role: guest
versions: rel/0.7.14
fixed_in:
branch: fix/w49-spacegfx
commits:
tests: campaign_space
signature:
docs: docs/known-issues.md#issue-gpu-overcommit-garbage, docs/06-testing.md#testing-gpu-overcommit
related: BUG-1096
---

## Report
rel/0.7.14 final check 20261114-005006 (6 lanes), test campaign_space (lane 7, RESULT OK). The key frames after the
Civ -> Space change (the guests load the host's world straight into Space):
- 02 advance-civ (01:18:01): guest-Bob and guest-Carol draw the solar system's background as flat coloured
  rectangles and a mosaic of small squares; Carol also has a big white translucent wedge over the scene and stripes
  of garbage geometry at the bottom. Dave and the host are clean.
- 03 play-space (01:20:24): Carol's HUD looks gone, the same white wedge and garbage stripes. Bob is clean again.
Sheets: frames/sheets/021_campaign_space_02_advance-civ.jpg, 022_campaign_space_03_play-space.jpg.

## Root cause
Not a mod bug: GPU memory overcommitted by the lab's load (DXVK 32-bit on RADV, RX 7900 GRE 16 GB).
Observed:
- Carol's "missing HUD" is drawn, with garbage geometry: a crop of the bottom of 03_play-space_guest-Carol.jpg shows
  the red Space palette panel, the green / blue bar and the money box smeared into long streaks at the HUD's place
  (vertex or index buffers with wrong contents). Nothing hid a UI layer; the same corruption hits the 3D scene (the
  wedge, the stripes) and the textures (the mosaic).
- No mod or SporeKit line about it in the three guests' logs: no `[WRN]`/`[ERR]` beyond net spikes, no overlay or
  Present cut-off (BUG-1024 paths), no fault, no D3D error, device Reset only at start; address space 3209-3303 of
  4095 MB free, largest block 1909 MB; 19-41 fps in Space. Carol's trace: no cinematic or pause at the 02 frame.
- At 01:18 the machine ran 22 test games at once (campaign 2, campaign_civ_jump 3, campaign_tc2 4,
  campaign_tribe_noclick 5, play_space 6 (2 games), campaign_space 7: five 4-player tests), all 1440x1080. MemAvailable
  went down to 4.9 GB in that test (the lab's mem line); the journal shows "system memory pressure is elevated" every
  minute from 01:10 to 01:20.
- VRAM measured on the same machine (/sys/class/drm/card1/device/mem_info_vram_used, every 10 s): 5.2 GB with 2
  games, 9.0 GB with 8, 10.1 GB avg / 10.8 max with 10, 11.2 avg / 11.8 GB max with 12: about 0.66 GB a game over a
  base of ~3.9 GB. 22 games: ~18.4 GB of a 16.0 GB card (16368 MB): ~2.4 GB had to live in GTT (system RAM) while
  that RAM was itself short.
- Same build, same path, quiet machine: campaign_space on lane 4 alone twice (4 games of this test plus at most 8
  others, 12 at most): every key frame clean on all four games, HUDs and co-op status in place, including a guest's
  leave and join in Space (Carol in 20261114-031143, as in the failing run). In the failing run itself
  campaign_civ_jump's guests made the same Space entry 5 minutes later (01:23:29) with clean frames.
- Earlier releases' campaign_space key frames (0.7.12 x3, 0.7.13 20261112-175447, fix/w49-tests 20261113-072523
  with the 0.7.14 kit changes): clean.
Inferred: the driver's eviction under VRAM + RAM pressure left some of Bob's and Carol's buffers with wrong contents
(the stripes at the HUD's place are its own vertices drawn from a bad buffer). Not proven at the driver level: no
amdgpu error in the kernel log at 01:18 (only the usual "VM memory stats ... non-zero when fini" at game ends).

## Fix
None in the mod. docs/known-issues.md#issue-gpu-overcommit-garbage (how to tell it on a key frame) and
docs/06-testing.md#testing-gpu-overcommit (the numbers, the game count to keep the VRAM under the card).

## Verification
no-fail-run: not a mod bug and not reproducible on a quiet lane: campaign_space on fix/w49-spacegfx (= rel/0.7.14
4dd8d2be, no code change) lab 20261114-025653 (lane 4, PASS, frames clean) and 20261114-031143 (lane 4, PASS,
frames clean, a guest's rejoin in Space); VRAM max 11.8 GB with 12 games during them.

## History
- 2026-11-14: opened.
- 2026-11-14: closed as not-reproduced (GPU memory overcommit under the 22-game final check), fix/w49-spacegfx.
