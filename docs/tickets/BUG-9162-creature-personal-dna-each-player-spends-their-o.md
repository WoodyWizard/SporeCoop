---
id: BUG-9162
title: Creature: personal DNA: each player spends their own DNA; the progress bar stays the party's (decision 2F)
status: open
kind: feature
stage: creature
reported: 2026-11-13
source: owner
role: both
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host + guest 2026-11-13: console gains and spends)
signature:
docs: docs/re/creature.md#creature-personal-dna
related: BUG-0466 BUG-0510 BUG-0351 BUG-9160
switch: creature.personal_dna
---

## Report
Owner decision 2F (2026-11-12): with everyone evolving on their own, "Everyone gets their own DNA." The progress (the brain and its bar) stays the party's.

## Root cause
Design. Before: one pool: the host's game paid every gain (its own, a guest's kill credited to the host's avatar, a guest's gain sent with MSG_DNA_ADD) and sent its budget and bar; a guest's spend was sent as a negative MSG_DNA_ADD (BUG-0466).

## Fix
creature.personal_dna (on), Creature only, while connected. Guest: its own gain is applied here (its budget and its bar) and still sent to the host (MSG_DNA_ADD); the host's total sets only its bar (cPlayer +0x10F0); no spend is sent. Host: a guest's gain goes to the bar only (dna_sync_raw with its own budget kept); a guest's spend is ignored; a guest's kill paid on the host's game (the ledger's Hunt part of that guest) goes to the bar here and to that guest as MSG_DNA_OWN (85, f32), which adds it to that guest's budget ("+N DNA from your hunt"). Off: the shared pool as before.

## Verification
no-fail-run: a feature. Live 2026-11-13, TITAN host + guest, `call Simulator::cCreatureGameData::AddEvolutionPoints cdecl f:N` (the game's own add, through the mod's hook): the guest +20: guest 138 -> 158, host 138, the bar 321 -> 341 on both; the host +10: host 148, guest 158, the bar 351 on both; the guest's DNA set to 100 (a spend): host 148, the guest still 100 after the host's totals; the guest's own editor (no vote) and back: guest 100, host 148, 0 faults. Not seen yet: a guest's kill paid as MSG_DNA_OWN.

## History
- 2026-11-13: opened.
- 2026-11-13: the sync check compared the DNA budgets and reported "MISMATCH ... dna: host 398.0, guest 250.0" for a
  guest that had spent its own; with personal DNA it compares the party's progress bar (coop.cpp p.dna). Live: OK with
  both guests after that.

