---
id: BUG-9170
title: Civ: a late joiner without a creature of their own goes into the creature editor first; their capital follows behind a loading screen (decision 1P / 14)
status: open
kind: feature
stage: civ
reported: 2026-11-13
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN Civ host + a late joiner on a fresh profile, 2026-11-13)
signature:
docs: docs/re/civ.md#civ-late-editor-first
related: BUG-9159 BUG-9164 BUG-9135 BUG-9150
switch: civ.late_editor_first
---

## Report
Owner decisions 1P and 14 (2026-11-12): a late joiner without a creature of their own in Civ (no clean species change known there): "1A: the creature editor first; meanwhile a blurred view ... with a progress ring and short steps". Before: the Co-op window asked them to choose a creature; the host founds no capital for a player on the host's species (LateWhy::NoOwnCreature).

## Root cause
Design. Tribe has it (BUG-9159): coop.cpp open_new_creature_editor (CreatureEditorExtraLarge, no creation, creations::begin_new_creature) and creations adopts what the editor saved when the game is back in Tribe. civ::late_prompt: 1 a guest without a nation playing no creature of its own, 2 its own creature (the host founds the capital).

## Fix
civ.late_editor_first (on). coop.cpp civ_editor_first_tick (guest, Civ): once connected 15 s with no world transfer or load queued, while civ::late_prompt is 1 and the game is not busy: open_new_creature_editor (late_create_offered also in Civ), tried every 3 s, 10 times a world, once opened not again in that world. creations adopts the saved creature in Civ too ("Your capital follows shortly."). While civ::late_prompt is 2 after that editor: the loading screen of Tribe ("Founding your capital", the ring, "Catching up to <host>...", "Matching tools...", ...; 90 s at most). The Creature join editor (BUG-9164) also waits while the host's world is on its way. Support log: "civ: editor first waits: late_prompt=..." (10 times).

## Verification
no-fail-run: a feature. Live 2026-11-13 on TITAN, host on a Civ save (Abber), a fresh-profile joiner set to play no creature of its own (`coop.creations choose creature none`): the creature editor opened from Civ (a new blob, 2,000 DNA); a mouth added, named, Done: "back from the editor, own creature 29209D86", "Your new creature is your creature for this game. Your capital follows shortly.", the host's world loaded after the editor, then the loading screen ("Founding your capital", "Matching tools...", screenshot). Second run with the timing fix: the world came at 03:45:11, "editor first waits: late_prompt=0" at 03:45:23, the editor at 03:45:28; left with the X: "No new creature was saved. Choose your own creature in the Co-op window (F9).", no second editor, 0 faults. Not seen: the capital founded for the joiner: both Civ saves on hand have four founder nations (the host: "civ late capital: NoCreature (player 1) has no party seat yet"), so the loading screen ends after 90 s there. Also on the MSI host (the same save): the same reason.

## History
- 2026-11-13: opened.
