---
id: BUG-1266
title: Cell: the host's electric zap is drawn on the guest's screen as a trail of giant white rings
status: fixed
kind: bug
stage: Cell
reported: 2026-11-18
source: key-frame review
role: guest
versions: 0.7.15.1
fixed_in: 0.7.16
branch: fix/w54-visual
commits: aa478bd4
tests: cell_attack_electric
signature:
docs: docs/re/cell.md#cell-gfx-held-effects
related: BUG-1243
---

## Report
Key-frame review of the 0.7.15.2 final check (lab 20261118-144132, sheet
`frames/sheets/007_cell_attack_electric_01_host_zaps.jpg`): while the host's cell zaps with an electric part, the
host's own screen shows a small yellow spark, the guest's screen shows a trail of giant white rings centred on the
host's cell, covering half the screen. The same picture is in the frames of rel/0.7.15.1 (20261118-110806) and
fix/w52-evoeditor (20261118-102744): older than 0.7.15.2. The test's checks (animations) pass; only the picture
shows it.

Reproduce: `tools/lab.py run cell_attack_electric`, then `tools/frames_review.py <run> --test cell_attack_electric`.

## Root cause
Static (Ghidra, GOG 2024) and the logs / frames of the earlier runs; no lab run yet (owner rule 2026-11-18: no lab
runs before the night run). docs/re/cell.md#cell-gfx-held-effects.
* The Cell graphics update `SporeCoop::CellGfx_Update` 0xE73F60 (once a frame, every cell) creates the effects a
  cell's graphics object keeps and drives them every frame until it stops them. The electric part has two:
  `CellGfx_ElectricField` 0xE6BC10, while the cell charges, effect 0x17AAB61F (or 0x5CA636DC) at the part with the
  uniform scale 2 x the zap range, moved and rescaled every frame, Stop(1) when the charge ends; and
  `CellGfx_ZapBolt` 0xE6B9B0, the lightning 0x7D84966F / 0xA1FB7133 on the zapped cell with a 21-point path from the
  attacker set every frame.
* The relay's world-effect hook (EffectWorld::CreateEffect) saw these as any world effect near the player and sent
  them (the guest with the electric part sent 0x17AAB61F, the propulsion ids and 0xF8EB2D4D in lab 20261118-131202).
  The receiver made a one-shot copy: the first frame's transform (the charge field at 2 x range), no following of
  the cell, no path, no Stop from the game; the relay stops it only after 8 s (kLiveSec). With a zap every ~3 s
  (8 zaps in 25 s in the test) the guest's screen held three or more such rings left along the host's way: the
  trail of giant white rings centred on the host's cell. Inferred, not measured: which of the two ids is the white
  ring (the field, by its place at the cell and its scale; the lightning is made at the target, usually beyond the
  relay's 1.8 m) and why the copy is bright where the host's own field is barely seen (the effect's timeline, cut on
  the host after the charge).
* Not the cause: a wrong id, or the Cell world scale: the transform the host's game set (scale included) is copied
  as it is (BUG-0465), and both games share the world scale of the level.

## Fix
`effects.cell_held` (game.cpp h_CellGfxUpdate on `SporeCoop::CellGfx_Update` marks every effect created inside the
update; effects.cpp process_world does not send a marked one; on_message drops 0x17AAB61F / 0x5CA636DC / 0x7D84966F /
0xA1FB7133 in Cell from an older sender). The partner's game runs the same update for its own copy of the cell, so the
propulsion and glows of a puppet stay its own game's. A partner's zap now shows no charge or lightning on the other
screen (the copy of the attacker does not charge); the zapped creature's hp loss stays. `coop.fix effects.cell_held
off`: sent and played (0.7.15). Tests read `coop.effects recent` (now with `scale=` and `held=`) and the line
`effects cell held: fix= made= not_sent= not_played=` of `coop.effects`.

## Verification
Pending the night run (no lab runs allowed while the fix was written). `coop_cell_attack_anim.py --electric` now also
checks what the guest's game created from the host's relay during the zaps: none of the effects the host's Cell
graphics update made (its `held=1` rows, the charge field ids), every one played with the host's scale; a control that
the host's game made its charge field; the key frame's note says what each screen must show.
* The bug on the code before the fix (no check yet then, the picture only): out/lab/20261118-144132 (rel/0.7.15.2) and
  out/lab/20261118-110806 (rel/0.7.15.1), frames `cell_attack_electric/01_host_zaps_guest.jpg`.
* FAIL and PASS runs of the new check: not run yet, the commands below (the night run).
Tonight (lanes 2 and 3, build first with `make -j8`):
* PASS expected: `SPORE_DETECTOR=20 python3 tools/lab.py run cell_attack_electric --lane 2 --retry 0 --no-build`
* FAIL expected (the twin): `SPORE_FIX_OFF=effects.cell_held SPORE_DETECTOR=20 python3 tools/lab.py run cell_attack_electric --lane 3 --retry 0 --no-build`
* regression: `SPORE_DETECTOR=20 python3 tools/lab.py run cell_far_fight effects_party_cell cell_electric --lane 2 --retry 0 --no-build`
* frames: `python3 tools/frames_review.py <run> --test cell_attack_electric` (the guest's frame: no white rings)

## History
- 2026-11-18: opened from the key-frame review of the 0.7.15.2 final check; seen since 0.7.15.1 at least; not a blocker of 0.7.15.2 (visual only, pre-existing), planned for 0.7.16.
- 2026-11-18: root cause from Ghidra and the earlier runs' logs (fix/w54-visual); fix `effects.cell_held` and the test written; lab pending the night run.
- 2026-11-23: released in 0.7.16.
