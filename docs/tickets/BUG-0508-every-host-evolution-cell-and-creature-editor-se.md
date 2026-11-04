---
id: BUG-0508
title: every host evolution (Cell and Creature editor) sends the whole world again: the guest drops to the galaxy menu and reloads, although the new body had already reached it
status: fixed
kind: bug
stage: Cell
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: guest
versions: 0.7.3.4, 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-no-reload
commits: b3bf9bd2, ae5fdf86, fc5ba8bc, fcb93d69, 299c8461, 01cc5fa6
tests: evolve_in_place_cell, evolve_in_place_creature (coop_evolve_in_place.py --case cell|creature)
signature:
docs: docs/re/creations.md#creations-evolve-in-place, docs/06-testing.md#testing-evolve-in-place
related: BUG-0019, BUG-0457, BUG-0412, BUG-0232, BUG-0516, BUG-0517
switch: world.evolve_in_place, creations.follow_evolution
---

## Report
0.7.3.4 forum post, the player's 'most annoying' problem: every time the host changes its creature in the editor, the guest's game syncs the world and loads again (goes to the main menu and joins back), although the world looked synced and the host's new creature was visible right before. The same player asks why the guest must accept (be ready for) opening the host's editor at all.
Log (guest, 0.7.3.4, report 20261102-231730, previous log 231251): 23:10:16 'host is back' from its Cell editor, 23:10:22 the guest receives the whole world (3.2 MB), 23:10:23 Cell -> Galaxy menu, 23:10:27 loads it again; same pattern after each host editor (Cell and Creature).
Target: 0.7.4 (Cell / Creature, a top complaint).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed in the code (rel/0.7.4, coop.cpp on_mode_change): by design the host marks every guest needs_world after an editor (`evolved`: 'evolved the species: everybody gets the new body', 3 s after the cinematic, BUG-0019 / BUG-0232), and follow_phase sends the save; the guest loads it through the galaxy menu. 0.7.4 does not change this.
Inferred (triage): the reload is needed only for what the creation records alone do not carry.
Observed on fix/w40-no-reload (lane 6, guest without own creations), each item in
docs/re/creations.md#creations-evolve-in-place:
* the host's puppet is rebuilt by the creation bundle before the world arrives (not by the reload);
* the reload gave the guest's own avatar the host's new species (Cell model / Creature SetSpecies) and the
  world's player species (cSpeciesManager +0xA4); without it both stayed the pre-editor ones;
* the host's own avatar takes its evolved species only with a save of the game (BUG-0517): skipping the share
  without a save left the host on its old creature;
* every Creature evolution plays a ~10 s cinematic and a hatchling avatar (age 0) for seconds: no migration sign;
  the migration (property 0x514A219, BUG-0019) is the one thing only the save carries.

## Fix
* Host (`world.evolve_in_place`, coop.cpp on_mode_change / follow_phase): after an editor inside a stage each guest is
  still marked and waits as before (BUG-0019 / BUG-0232 wait); where the share would go out, a guest of this version
  whose latest state is in the same world and the stage (or an editor) gets no world. In Creature the host saves its
  own game once (not sent) so its avatar takes the new species. The world still goes: a migration started (property
  0x514A219), the guest left the world / stage, an older guest (no CAP_VERSION), or a Creature guest whose avatar
  still reports the pre-editor species 30 s later (failed apply).
* Guest (`creations.follow_evolution`, creations.cpp): a guest that plays the host's creature as the world's species
  (no own creation, no choice) follows the host's new bundle in this stage world (own cell model + level rebuild,
  SetSpecies on the avatar), and every guest moves the world's player species (+0xA4) from the host's previous key to
  the new one.
* The vote question: answered in docs/re/creations.md#creations-evolve-in-place; not relaxed (BUG-0516).

## Verification
* FAIL (`SPORE_FIX_OFF=world.evolve_in_place`): Cell out/lab/20261103-173049 (guest Cell -> GalaxyGameEntry -> Cell,
  host shares 1 -> 2); Creature out/lab/20261103-174502 (GalaxyGameEntry, LoadGame, shares 1 -> 2).
* PASS: Cell out/lab/20261103-172813 (no menu, no share, puppet 7C76F89F 'Inplacecell', own cell 4DA56CA5 -> 291431B5);
  Creature out/lab/20261103-174221 (no menu, no share, puppet 7C530FCB 'Burpee', own avatar 065CBE70 -> 29143508,
  world player species 29143508 on both, host saved once). SPORE_DETECTOR=20: only known signatures.
* Regression (lane 6, SPORE_DETECTOR=20): out/lab/20261103-175935, 23/24 passed (smoke + cell_coop, creature,
  guest_editor, editor_sync_guest, editor_sync_host, editor_desync, stage_switch, campaign_short, play_creature, unit,
  evolve_world: the migration still sends the world, both evolve_in_place tests); ui skipped (exclusive, lanes 1-5).
* On the way: out/lab/20261103-173652 (no save on the host: its avatar stayed 065CBE70, BUG-0517).

## History
- 2026-11-03: opened.
- 2026-11-03: opened from the 0.7.3.4 forum post and its logs (w40 triage).
- 2026-11-03: fixed on fix/w40-no-reload (evolve in place, Cell and Creature; FAIL / PASS runs above).
- 2026-11-03: released in 0.7.4.
