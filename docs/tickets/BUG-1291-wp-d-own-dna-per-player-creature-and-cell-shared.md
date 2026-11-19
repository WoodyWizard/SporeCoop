---
id: BUG-1291
title: WP-D: own DNA per player (Creature and Cell), shared pool as a session setting
status: open
kind: feature
stage: Cell, Creature
reported: 2026-11-19
source: plan
role: 
versions: 
fixed_in:
branch: feat/w56-dna
commits: db0796ed 59a4a1fd 48979cc9 006c2aab
tests: tests/dna_tests.cpp, tools/tests/coop_dna_own.py (spend, shared, reload, rejoin, store, editors), tools/tests/coop_evo_screens.py --part cell-dna [--shared]
signature:
docs: docs/03-gameplay.md#gameplay-own-dna, docs/re/creature.md#creature-own-dna, docs/06-testing.md#testing-dna-own
related: BUG-0466, BUG-0510, BUG-0630, BUG-1249
---

## Report
Plan item WP-D of 0.7.16 (owner decision 2026-11-19): every player has its own DNA by default; a session setting
turns on the old shared pool; Rivals (0.8) always separate. Until now Cell and Creature played one host pool
(MSG_DNA 23 / MSG_DNA_ADD 24): every editor of every player spent from it (the class of BUG-0466, BUG-0630, BUG-1249).
Design input: out/research/lineages-systems.md section D (model "party income, own wallet").

## Root cause
Not a bug: a design change. The host's game DNA was the only DNA; a guest's spend came off it (BUG-0466) and the host's
total set every guest's DNA.

## Fix
* `mods/coop/dna_own.h` (portable, tests/dna_tests.cpp): the host's wallets per guest (Creature budget, Cell spent
  count, per world), the bar observer (the host's bar step > 0 in Creature goes to every wallet of the world: every
  gain of the party counts for everybody once), the store record `dna.own` v1, MSG_DNA's tail.
* Host (dna.cpp, coop.cpp, session_glue.cpp): MSG_DNA per guest with that guest's own budget and a tail after the pace
  byte (u8 flags, f32 party bar, u32 world); a guest's MSG_DNA_ADD < 0 comes off its wallet only; first sight of a
  guest = the host's DNA now; the wallets go into the per-player store at every save and come back when the host's
  world opens (a reload rolls them back with the host's own DNA).
* Guest: an own total is set with SetEvolutionPoints and the bar raised to the party's first (game::dna_set_and_bar,
  the fork's order); never lowered.
* Cell (cellsync.cpp, wsync.cpp): wsync row 19 `cell_spent_own` (key the player, v0 its own spent count); a guest's
  own cell editor spend goes to its count; the host's S+0x2C keeps only the host's spends.
* Setting `all.shared_dna` (default off = own; read as off in Rivals mode); creature.mode host is shared too.
* A peer below creature / npc 1.2 (0.7.15.2) plays the shared pool with the host (dna_own::peer_reads); a new guest of
  an older host gets no tail: shared.
* Families creature 1.1 -> 1.2, npc 1.1 -> 1.2 (locked).
* Switches: `creature.own_dna` (off: one pool), `creature.dna_bar_set` (off: AddEvolutionPoints of the difference),
  `creature.own_dna_store` (off: a guest starts again at the host's DNA when the host's world opens).

## Verification
Unit: tests/dna_tests.cpp in `make test` (PASS). Lab: NOT RUN yet (the orchestrator's "lab ok" did not come before
the hand-over; the CPU was busy). The runs to do, lanes 2 and 3, after `make -j8`:
  SPORE_DETECTOR=20 python3 tools/lab.py run dna_own dna_own_shared dna_own_reload dna_own_rejoin --lane 2 --lane 3 --retry 0 --no-build
  SPORE_DETECTOR=20 python3 tools/lab.py run dna_own_off dna_own_shared_off dna_own_reload_off dna_own_rejoin_off --lane 2 --lane 3 --retry 0 --no-build
  SPORE_DETECTOR=20 python3 tools/lab.py run dna_own_store dna_own_editors evo_screens_cell_dna --lane 2 --lane 3 --retry 0 --no-build
  SPORE_DETECTOR=20 python3 tools/lab.py run dna_own_store_off dna_own_editors_off evo_screens_cell_dna_off --lane 2 --lane 3 --retry 0 --no-build
  SPORE_DETECTOR=20 python3 tools/lab.py run evo_bar evo_bar_editors dna_pace editor_sync_guest dna_spend evo_screens_cell_dna_shared --lane 2 --lane 3 --retry 0 --no-build
Then `tools/frames_review.py <run> --test dna_own_editors` (the two editors' DNA readouts must differ by 30).

## History
- 2026-11-19: opened.
- 2026-11-19: feat/w56-dna: code, unit tests, lab tests written; lab runs wait for the orchestrator.
- 2026-11-19: fix/w57-dna: the lab gates compared one shared DNA (BUG-1310, fixed) and a stray shared total after the host's editor (BUG-1311, fixed). PASS on the merged tree 20261119-120634: dna_own, dna_own_reload, dna_own_rejoin, dna_own_store, dna_own_shared, dna_spend, dna_pace, editor_sync_guest, evo_bar_editors; twins FAIL for their reasons: dna_own_off, dna_own_reload_off, dna_own_rejoin_off, dna_own_store_off (the guest starts again at the host's DNA, 150 / 150, as the night's 20261119-105127). dna_own_editors is another agent's.
