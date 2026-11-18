---
id: BUG-9165
title: Creature: a guest's own DNA is lost when the host's world is loaded again
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: qa
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host + 2 guests 2026-11-13: coop.share after a spend, the fix on and off)
signature:
docs: docs/re/creature.md#creature-join-editor-first
related: BUG-9162 BUG-9164 BUG-0466
switch: creature.keep_own_dna
---

## Report
QA 2026-11-13 (BUG-9164's test): a joiner spent 25 DNA in its own editor (398 -> 373); its new creature got a nest, the host sent the world again, and after the load the joiner had 398 again: the host's number.

## Root cause
Observed: with personal DNA (BUG-9162) a guest's spendable DNA lives only in its own game (cCreatureGameData, the evolution points global). A load of the host's world (a share at a stage change, a new home nest, coop.share, a rejoin) sets it from the host's save, which holds the host's budget. The bar is the host's and comes back with MSG_DNA anyway.

## Fix
creature.keep_own_dna (on), coop.cpp own_dna_tick, guests in Creature with personal DNA: the last own DNA seen in this world is kept (and written to SporeCoop\worlds.ini [dna] <world crc>, at most every 5 s, for a later session); when the avatar stands in a newly loaded copy of the world (game::stage_epoch(kCreature) changed; an editor visit keeps the epoch, so a spend stays spent) and the DNA differs, it is put back with dna_sync_raw (the bar kept). creations::world_note / world_note_set: per-world notes in worlds.ini.

## Verification
no-fail-run: manual on TITAN (no lab lane on Windows). Live 2026-11-13, host + 2 guests: the joiner's DNA set to 250 (SetEvolutionPoints, a spend), worlds.ini [dna] D296F25C=250 written, `coop.share 2` on the host: after the load "own DNA 250 put back after a load of the world (the save had 398)", the joiner 250, the host 398. Fix off (`coop.fix creature.keep_own_dna off`): set to 200, `coop.share 2`: after the load the joiner had 398 (FAIL as reported). Second run (01:51): a world share started 2 s after the own editor's Done (the joiner still in the editor, the load deferred): "own DNA 250 put back after a load of the world (the save had 398)", the spend in that editor kept. The first version recorded the DNA only while the game was not busy and put back the value from before the editor (250 instead of 225 at 01:43): now it is recorded while busy too, only the put-back waits. Not covered: a guest's gain between the load and the put-back is overwritten.

Cross-PC 2026-11-13: the MSI spent 25 in its join editor (398 -> 373); after the host's world came for its new nest: "own DNA 373 put back after a load of the world (the save had 398)"; again after later loads (the save had 548).

## History
- 2026-11-13: opened.
