---
id: BUG-0519
title: Cell: a partner's own cell (puppet) may be drawn off while a level change rescales the worlds
status: fixed
kind: desync
stage: Cell
reported: 2026-11-03
source: 
role: guest
versions: 
fixed_in: 0.7.5
branch: fix/w41-cell-left
commits: c3e4f926
tests: cell_puppet_scale (tools/tests/coop_cell_puppet_scale.py)
signature:
docs: docs/re/cell.md#cell-puppet-scale
related: BUG-0511
---

## Report
Found while fixing BUG-0511 (fix/w40-misc), not measured: the players' own cells travel in MSG_STATE (`modes::cell_fill` / `cell_drive`) in the sender's world scale, without the scale. A level change rescales each game's world over ~3.5 s at slightly different moments (docs/re/cell.md#cell-world-scale), so a partner's puppet is probably drawn off by up to the same 10+ m for those seconds (the NPC mirrors were 14-18 m off before BUG-0511's fix). Possibly the "visual glitch when growing" of BUG-0511's report. To reproduce: coop_cell_grow.py with the host's `coop.avatar` pos vs the guest's `puppet pos=` (coop.status), both times their world_scale.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lane 4, coop_cell_puppet_scale.py): during each level change the partner's puppet was drawn up to 10 m from
where the partner's cell is (p90 8.0 m host draws the guest, 4.2 m guest draws the host; lab 20261104-021951): the
players' own cells travel in MSG_PLAYER_STATE in the sender's world scale and were drawn unchanged while the two
scales differ (host 2.34 / guest 1.90 at one sample).

## Fix
`cell.puppet_scale`: PlayerState carries an appended `scale` (the sender's G+0x514C in Cell; older readers stop
before it, an older sender reads as 0 = no conversion); the snapshot buffer blends two states of different scales
in the newer one's (net/interp.h); cell_drive (modes.cpp) draws pos, velocity and size times sender / own scale.
Off: 0.7.4 (also the old blend). `coop.pool scalepos` prints scale, own cell and puppets of one frame.

## Verification
FAIL: SPORE_FIX_OFF=cell.puppet_scale cell_puppet_scale, lab 20261104-021951 (p90 8.0 / 4.2 m).
PASS: cell_puppet_scale, lab 20261104-021835 (p90 0.5 / 0.3 m). Unit: net_tests (the appended field, an old
sender's state, the blend across two scales), fuzz_decoders (base prefix of 67 bytes).

## History
- 2026-11-03: opened.
- 2026-11-04: fix/w41-cell-left: the puppet drawn in the receiver's scale (cell.puppet_scale); fixed (unreleased).
- 2026-11-04: released in 0.7.5.
